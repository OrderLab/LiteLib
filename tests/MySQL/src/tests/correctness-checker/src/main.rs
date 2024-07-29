use async_mutex::Mutex;
use hashbag::HashBag;
use indicatif::{ProgressBar, ProgressStyle};
use mysql::prelude::*;
use mysql::*;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

#[derive(Clone, Debug, PartialEq, Eq)]
struct Data {
    k: Option<i32>,
    c: Option<String>,
    pad: Option<String>,
}

static TABLE_COUNT: usize = 1;
static TABLE_SIZE: i32 = 10;
// static TABLE_SIZE: i32 = 1000000;
static TEST_DURATION: Duration = Duration::from_secs(60);
static THREAD_COUNT: usize = 1;
static RANGE_SIZE: i32 = 100;

fn get_rand_db_id() -> i32 {
    (rand::random::<u32>() % (TABLE_COUNT as u32) + 1) as i32
}

fn get_rand_id() -> i32 {
    (rand::random::<u32>() % (TABLE_SIZE as u32) + 1) as i32
}

fn get_rand_str(group: i32) -> String {
    let mut c = String::new();
    for i in 0..group {
        for _ in 0..11 {
            // rand a digit
            c.push((rand::random::<u8>() % 10 + 48) as char);
        }
        if i != group - 1 {
            c.push('-');
        }
    }
    c
}

fn get_rand_c() -> String {
    get_rand_str(10)
}

fn get_rand_pad() -> String {
    get_rand_str(5)
}

fn point_selects(conn: &mut PooledConn, db_id: i32, id: i32) -> Result<Vec<Data>> {
    conn.query_map(
        format!("SELECT c FROM sbtest{} WHERE id = {}", db_id, id),
        |c| Data {
            k: Some(0),
            c,
            pad: Some("".to_string()),
        },
    )
}

fn simple_ranges(
    conn: &mut PooledConn,
    db_id: i32,
    lower_bound: i32,
    upper_bound: i32,
) -> Result<Vec<Data>> {
    conn.query_map(
        format!(
            "SELECT c FROM sbtest{} WHERE id BETWEEN {} AND {}",
            db_id, lower_bound, upper_bound
        ),
        |c| Data {
            k: Some(0),
            c,
            pad: Some("".to_string()),
        },
    )
}

fn sum_ranges(
    conn: &mut PooledConn,
    db_id: i32,
    lower_bound: i32,
    upper_bound: i32,
) -> Result<Vec<Data>> {
    conn.query_map(
        format!(
            "SELECT SUM(k) FROM sbtest{} WHERE id BETWEEN {} AND {}",
            db_id, lower_bound, upper_bound
        ),
        |k| Data {
            k,
            c: Some("".to_string()),
            pad: Some("".to_string()),
        },
    )
}

fn order_ranges(
    conn: &mut PooledConn,
    db_id: i32,
    lower_bound: i32,
    upper_bound: i32,
) -> Result<Vec<Data>> {
    conn.query_map(
        format!(
            "SELECT c FROM sbtest{} WHERE id BETWEEN {} AND {} ORDER BY c",
            db_id, lower_bound, upper_bound
        ),
        |c| Data {
            k: Some(0),
            c,
            pad: Some("".to_string()),
        },
    )
}

fn distinct_ranges(
    conn: &mut PooledConn,
    db_id: i32,
    lower_bound: i32,
    upper_bound: i32,
) -> Result<Vec<Data>> {
    conn.query_map(
        format!(
            "SELECT DISTINCT c FROM sbtest{} WHERE id BETWEEN {} AND {} ORDER BY c",
            db_id, lower_bound, upper_bound
        ),
        |c| Data {
            k: Some(0),
            c,
            pad: Some("".to_string()),
        },
    )
}

fn index_updates(conn: &mut PooledConn, db_id: i32, id: i32) -> Result<()> {
    conn.query_drop(format!(
        "UPDATE sbtest{} SET k = k + 1 WHERE id = {}",
        db_id, id
    ))
}

fn non_index_updates(conn: &mut PooledConn, db_id: i32, id: i32, new_c: String) -> Result<()> {
    conn.query_drop(format!(
        "UPDATE sbtest{} SET c = '{}' WHERE id = {}",
        db_id, new_c, id
    ))
}

fn deletes(conn: &mut PooledConn, db_id: i32, id: i32) -> Result<()> {
    conn.query_drop(format!("DELETE FROM sbtest{} WHERE id = {}", db_id, id))
}

fn inserts(conn: &mut PooledConn, db_id: i32, id: i32, data: Data) -> Result<()> {
    conn.query_drop(format!(
        "INSERT INTO sbtest{} (id, k, c, pad) VALUES ({}, {}, '{}', '{}')",
        db_id,
        id,
        data.k.unwrap(),
        data.c.unwrap(),
        data.pad.unwrap()
    ))
}

fn init() -> Pool {
    let pool_opts = PoolOpts::default()
        .with_constraints(
            PoolConstraints::new(
                std::cmp::min(THREAD_COUNT, TABLE_COUNT as usize),
                std::cmp::max(THREAD_COUNT, TABLE_COUNT as usize),
            )
            .unwrap(),
        )
        .with_reset_connection(false);
    let opts = mysql::OptsBuilder::new()
        .ip_or_hostname(Some("127.0.0.1"))
        .tcp_port(59999)
        .user(Some("sbtest"))
        .pass(Some("password"))
        .db_name(Some("sbtest"))
        .prefer_socket(false)
        .max_allowed_packet(Some(4194304))
        .pool_opts(pool_opts);
    Pool::new(opts).unwrap()
}

// #[tokio::main]
// async fn main() {
//     let pool = init();
//     let mut conn = pool.get_conn().unwrap();
//     println!("Result: {:?}", point_selects(&mut conn, 1, 3));
//     println!("Result: {:?}", non_index_updates(&mut conn, 1, 3, "hello".to_string()));
//     println!("Result: {:?}", point_selects(&mut conn, 1, 3));
// }

#[tokio::main]
async fn main() {
    let pool = init();

    let tables: Arc<Mutex<Vec<HashMap<i32, Data>>>> = Arc::new(Mutex::new(Vec::new()));
    for _ in 0..TABLE_COUNT + 1 {
        let mut tables = tables.lock().await;
        tables.push(HashMap::new());
    }

    // -------------------------- Fetching the current database ---------------------------
    let mut handles = Vec::new();
    let bar = ProgressBar::new((TABLE_SIZE * TABLE_COUNT as i32) as u64)
        .with_prefix("Fetching the database");
    bar.set_style(
        ProgressStyle::with_template(
            "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
        )
        .unwrap(),
    );
    for i in 1..TABLE_COUNT + 1 {
        let bar = bar.clone();
        let pool = pool.clone();
        let tables = tables.clone();
        let handle = tokio::spawn(async move {
            for j in 1..TABLE_SIZE + 1 {
                let mut conn = pool.get_conn().unwrap();
                match conn.query_map(
                    format!("SELECT k, c, pad FROM sbtest1 WHERE id = {}", j),
                    |(k, c, pad)| Data { k, c, pad },
                ) {
                    Ok(row) => {
                        let mut tables = tables.lock().await;
                        if row.len() > 0 {
                            tables[i as usize].insert(j as i32, row[0].clone());
                        }
                    }
                    Err(e) => {
                        println!("Error: {:?}", e);
                    }
                }
                bar.inc(1);
            }
        });
        handles.push(handle);
    }
    for handle in handles {
        handle.await.unwrap();
    }
    bar.finish();
    println!("\nFinished fetching the database");

    // ------------------------- Perform the benchmarking -----------------------------
    let mut handles = Vec::new();
    let bar = ProgressBar::new(TEST_DURATION.as_secs() as u64).with_prefix("Fetching the database");
    bar.set_style(
        ProgressStyle::with_template(
            "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
        )
        .unwrap(),
    );
    let start = std::time::Instant::now();
    for _ in 0..THREAD_COUNT {
        let bar = bar.clone();
        let pool = pool.clone();
        let tables = tables.clone();
        let handle = tokio::spawn(async move {
            while (std::time::Instant::now() - start) < TEST_DURATION {
                bar.set_position((std::time::Instant::now() - start).as_secs());
                let mut conn = pool.get_conn().unwrap();
                let mut tables = tables.lock().await;
                {
                    // point selects
                    let db_id = get_rand_db_id();
                    let id = get_rand_id();
                    let result = match point_selects(&mut conn, db_id, id) {
                        Ok(result) => result,
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!(
                                    "point_selects db_id: {}, id: {}, error: {:?}",
                                    db_id, id, e
                                );
                            }
                        }
                    };
                    if let Some(expected) = tables[db_id as usize].get(&id) {
                        assert_eq!(
                            result[0].c, expected.c,
                            "point_select db_id: {}, id: {}",
                            db_id, id
                        );
                    }
                }
                {
                    // simple ranges
                    let db_id = get_rand_db_id();
                    let lower_bound = get_rand_id();
                    let upper_bound = std::cmp::min(lower_bound + RANGE_SIZE, TABLE_SIZE);
                    let result = match simple_ranges(&mut conn, db_id, lower_bound, upper_bound) {
                        Ok(result) => result,
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!(
                                    "simple_ranges db_id: {}, lower_bound: {}, upper_bound: {}, error: {:?}",
                                    db_id, lower_bound, upper_bound, e
                                );
                            }
                        }
                    };
                    let expected: HashBag<String> = HashBag::from_iter(
                        tables[db_id as usize]
                            .iter()
                            .filter(|(k, _)| *k >= &lower_bound && *k <= &upper_bound)
                            .map(|(_, v)| v.c.clone().unwrap()),
                    );
                    let actual: HashBag<String> =
                        HashBag::from_iter(result.iter().map(|v| v.c.clone().unwrap()));
                    assert_eq!(
                        expected, actual,
                        "simple_ranges db_id: {}, lower_bound: {}, upper_bound: {}",
                        db_id, lower_bound, upper_bound
                    );
                }
                {
                    // sum ranges
                    let db_id = get_rand_db_id();
                    let lower_bound = get_rand_id();
                    let upper_bound = std::cmp::min(lower_bound + RANGE_SIZE, TABLE_SIZE);
                    let result = match sum_ranges(&mut conn, db_id, lower_bound, upper_bound) {
                        Ok(result) => result,
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!(
                                    "sum_ranges db_id: {}, lower_bound: {}, upper_bound: {}, error: {:?}",
                                    db_id, lower_bound, upper_bound, e
                                );
                            }
                        }
                    };
                    let mut sum = 0;
                    for i in lower_bound..upper_bound + 1 {
                        if let Some(expected) = tables[db_id as usize].get(&i) {
                            sum += expected.k.unwrap();
                        }
                    }
                    assert_eq!(
                        result[0].k.unwrap(),
                        sum,
                        "sum_ranges db_id: {}, upper_bound: {}, upper_bound: {}",
                        db_id,
                        lower_bound,
                        upper_bound
                    );
                }
                {
                    // order ranges
                    let db_id = get_rand_db_id();
                    let lower_bound = get_rand_id();
                    let upper_bound = std::cmp::min(lower_bound + RANGE_SIZE, TABLE_SIZE);
                    let result = match order_ranges(&mut conn, db_id, lower_bound, upper_bound) {
                        Ok(result) => result,
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!(
                                    "order_ranges db_id: {}, lower_bound: {}, upper_bound: {}, error: {:?}",
                                    db_id, lower_bound, upper_bound, e
                                );
                            }
                        }
                    };
                    let mut expected: Vec<String> = tables[db_id as usize]
                        .iter()
                        .filter(|(k, _)| *k >= &lower_bound && *k <= &upper_bound)
                        .map(|(_, v)| v.c.clone().unwrap())
                        .collect();
                    expected.sort();
                    let actual: Vec<String> = result.iter().map(|v| v.c.clone().unwrap()).collect();
                    assert_eq!(
                        expected, actual,
                        "order_ranges db_id: {}, lower_bound: {}, upper_bound: {}",
                        db_id, lower_bound, upper_bound
                    );
                }
                {
                    // distinct ranges
                    let db_id = get_rand_db_id();
                    let lower_bound = get_rand_id();
                    let upper_bound = std::cmp::min(lower_bound + RANGE_SIZE, TABLE_SIZE);
                    let result = match distinct_ranges(&mut conn, db_id, lower_bound, upper_bound) {
                        Ok(result) => result,
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!(
                                    "distinct_ranges db_id: {}, lower_bound: {}, upper_bound: {}, error: {:?}",
                                    db_id, lower_bound, upper_bound, e
                                );
                            }
                        }
                    };
                    let mut expected: Vec<String> = Vec::from_iter(
                        tables[db_id as usize]
                            .iter()
                            .filter(|(k, _)| *k >= &lower_bound && *k <= &upper_bound)
                            .map(|(_, v)| v.c.clone().unwrap()),
                    );
                    expected.sort();
                    expected.dedup();
                    let actual: Vec<String> =
                        Vec::from_iter(result.iter().map(|v| v.c.clone().unwrap()));
                    assert_eq!(
                        expected, actual,
                        "distinct_ranges db_id: {}, lower_bound: {}, upper_bound: {}",
                        db_id, lower_bound, upper_bound
                    );
                }
                {
                    // index updates
                    let db_id = get_rand_db_id();
                    let id = get_rand_id();
                    match index_updates(&mut conn, db_id, id) {
                        Ok(_) => {}
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!(
                                    "index_updates db_id: {}, id: {}, error: {:?}",
                                    db_id, id, e
                                );
                            }
                        }
                    }
                    if let Some(data) = tables[db_id as usize].get_mut(&id) {
                        data.k = Some(data.k.unwrap() + 1);
                    }
                }
                {
                    // non-index updates
                    let db_id = get_rand_db_id();
                    let id = get_rand_id();
                    let new_c = get_rand_c();
                    match non_index_updates(&mut conn, db_id, id, new_c.clone()) {
                        Ok(_) => {}
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!(
                                    "non_index_updates db_id: {}, id: {}, error: {:?}",
                                    db_id, id, e
                                );
                            }
                        }
                    }
                    if let Some(data) = tables[db_id as usize].get_mut(&id) {
                        data.c = Some(new_c);
                    }
                }
                {
                    // deletes and inserts
                    let db_id = get_rand_db_id();
                    let id = get_rand_id();
                    match deletes(&mut conn, db_id, id) {
                        Ok(_) => {}
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!("deletes db_id: {}, id: {}, error: {:?}", db_id, id, e);
                            }
                        }
                    }
                    tables[db_id as usize].remove(&id);
                    let data = Data {
                        k: Some(0),
                        c: Some(get_rand_c()),
                        pad: Some(get_rand_pad()),
                    };
                    match inserts(&mut conn, db_id, id, data.clone()) {
                        Ok(_) => {}
                        Err(e) => {
                            if let mysql::Error::IoError(_) = e {
                                println!("IO error: {:?}", e);
                                continue;
                            } else if let mysql::Error::CodecError(e) = e {
                                println!("Codec error: {:?}", e);
                                continue;
                            } else {
                                panic!("inserts db_id: {}, id: {}, error: {:?}", db_id, id, e);
                            }
                        }
                    }
                    tables[db_id as usize].insert(id, data);
                }
            }
        });
        handles.push(handle);
    }
    for handle in handles {
        handle.await.unwrap();
    }
}
