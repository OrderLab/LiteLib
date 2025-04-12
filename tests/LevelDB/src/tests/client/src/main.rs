use async_mutex::Mutex;
use deadpool_redis::{redis::cmd, CustomRedisConnection as RedisConnection, Runtime};
use dotenvy::dotenv;
use duration_str::deserialize_duration;
use indicatif::{ProgressBar, ProgressStyle};
use rand::distributions::{Alphanumeric, Distribution};
use rand::Rng;
use std::process::Command;
use std::sync::{atomic::AtomicBool, Arc};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep, sleep_until, Duration, Instant};

#[derive(Debug, Clone, serde::Deserialize)]
enum ExperimentType {
    Full,
    Checkpoint(usize),
    Lite(usize, String),
    Ebpf(usize, String),
}

#[derive(Debug, Clone, serde::Deserialize)]
struct RemoteScriptConfig {
    root_dir: String,
    experiment_type: ExperimentType,
    cpu_limit: f64,
    remote_addr: String,
    remote_ssh_port: String,
    write_buffer_size: usize,
    #[serde(deserialize_with = "deserialize_duration")]
    crash_time: Duration,
}

#[derive(Debug, PartialEq, Clone, Copy, serde::Deserialize)]
enum KeyDistribution {
    Sequential,
    Zipf(f64),
}

#[derive(Debug, serde::Deserialize)]
struct BenchmarkConfig {
    num_keys: usize,
    key_length: usize,
    value_length: usize,
    #[serde(deserialize_with = "deserialize_duration")]
    test_duration: Duration,
    rps: usize,
    init_rps: usize,
    key_distribution: KeyDistribution,
    write_ratio: f64,
    #[serde(deserialize_with = "deserialize_duration")]
    timeout: Duration,
    retry_count: usize,
    inital_iter_count: usize,
    enable_connection_pool: bool,
    check_correctness: bool,
    work_dir: String,
    file_prefix: String,
    remote_script: Option<RemoteScriptConfig>,
}

#[derive(Debug, serde::Deserialize)]
struct Config {
    benchmark: BenchmarkConfig,
    redis: deadpool_redis::Config,
}

impl Config {
    pub fn from_env() -> Result<Self, config::ConfigError> {
        let cfg = config::Config::builder()
            .add_source(config::File::with_name("env.yaml"))
            .add_source(config::Environment::default().separator("__"))
            .build()?
            .try_deserialize::<Config>()?;

        Ok(cfg)
    }
}

#[derive(Debug, PartialEq, serde::Serialize)]
enum Status {
    Success,
    Miss,
    Timeout,
    Error,
    // TransactionError,
}

#[derive(Debug, serde::Serialize)]
struct QueryRecord {
    status: Status,
    request: Duration,
    response: Duration,
}

#[derive(Debug, serde::Serialize)]
struct Record {
    i: usize,
    key: usize,
    begin: Duration,
    queries: Vec<QueryRecord>,
}

fn generate_key(i: usize, length: usize) -> String {
    format!("{:0width$}", i, width = length)
}

fn get_last_n_char(string: &str, n: usize) -> &str {
    let len = string.len();
    if len < n {
        return string;
    }
    &string[len - n..]
}

fn get_suffix_from_value(value: &str) -> usize {
    value.split('_').last().unwrap().parse().unwrap_or(0)
}

macro_rules! update_conn_if_not_established {
    ($conn:expr, $timeout:expr) => {
        if $conn.conn.is_none() {
            $conn.conn = $conn.client.get_connection_with_timeout($timeout).ok();
            if $conn.conn.is_none() {
                // println!("Failed to establish connection");
            }
        }
    };
}

async fn do_query(
    i: usize,
    conn: &mut RedisConnection,
    key_index: usize,
    key: &str,
    base_value: &str,
    old_suffix_expected: &mut usize,
    is_write: bool,
    timeout: Duration,
    stale: Arc<AtomicBool>,
) -> QueryRecord {
    let new_suffix_expected = *old_suffix_expected + is_write as usize;
    let old_value_expected = format!("{}_{}_{}", base_value, key_index, *old_suffix_expected);
    let new_value_expected = format!("{}_{}_{}", base_value, key_index, new_suffix_expected);
    let request_time = SystemTime::now();
    update_conn_if_not_established!(conn, timeout);
    let result = match &mut conn.conn {
        Some(conn) => {
            conn.set_read_timeout(Some(timeout - request_time.elapsed().unwrap()))
                .expect("set_read_timeout failed");
            conn.set_write_timeout(Some(timeout - request_time.elapsed().unwrap()))
                .expect("set_write_timeout failed");
            match is_write {
                true => cmd("GETSET")
                    .arg(&key)
                    .arg(&new_value_expected)
                    .query::<Option<String>>(conn),
                false => cmd("GET").arg(&key).query::<Option<String>>(conn),
            }
        }
        None => Err(redis::RedisError::from(std::io::Error::new(
            std::io::ErrorKind::TimedOut,
            "Operation timed out",
        ))),
    };
    let response_time = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards");
    let request_time = request_time
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards");
    match result {
        Ok(new_value) => {
            match stale.load(std::sync::atomic::Ordering::SeqCst) {
                false => {
                    let mut status = Status::Success;
                    match new_value {
                        Some(new_value) => {
                            if new_value != old_value_expected {
                                let actual_suffix = get_suffix_from_value(&new_value);
                                if actual_suffix < *old_suffix_expected {
                                    eprintln!(
                                        "\ni: {}, key: {}, expected value: {:?}, actual value: {:?}\n",
                                        i,
                                        key,
                                        get_last_n_char(&old_value_expected, 10),
                                        get_last_n_char(&new_value, 10)
                                    );
                                    status = Status::Error;
                                    stale.store(true, std::sync::atomic::Ordering::SeqCst);
                                } else {
                                    // timeout comes after the response is sent
                                    *old_suffix_expected = actual_suffix;
                                    // eprintln!(
                                    //     "\ni: {}, key: {}, expected value: {:?}, actual value: {:?}, update expected value\n",
                                    //     i,
                                    //     key,
                                    //     get_last_n_char(&old_value_expected, 10),
                                    //     get_last_n_char(&new_value, 10)
                                    // );
                                }
                            } else {
                                *old_suffix_expected = new_suffix_expected;
                            }
                        }
                        None => {
                            eprintln!("\ni: {}, key: {} can't get value\n", i, key);
                            status = Status::Miss;
                        }
                    };
                    QueryRecord {
                        status,
                        request: request_time,
                        response: response_time,
                    }
                }
                true => {
                    QueryRecord {
                        status: Status::Error, // modification based on stale value is also stale
                        request: request_time,
                        response: response_time,
                    }
                }
            }
        }
        Err(e) => {
            // println!("request error i: {}, key: {}, error: {:?}", i, key, e);
            QueryRecord {
                status: if e.detail() != Some("flow control enabled") {
                    conn.is_valid = false;
                    Status::Timeout
                } else {
                    Status::Error
                },
                // status: Status::Timeout,
                request: request_time,
                response: response_time,
            }
        }
    }
}

#[tokio::main]
async fn main() {
    dotenv().ok();
    let cfg = Config::from_env().unwrap();
    println!("{:?}", cfg);
    let num_requests = cfg.benchmark.test_duration.as_secs() as usize * cfg.benchmark.rps;
    let init_interval = Duration::from_secs_f64(1.0 / cfg.benchmark.init_rps as f64);
    let interval = Duration::from_secs_f64(1.0 / cfg.benchmark.rps as f64);
    let base_value: String = rand::thread_rng()
        .sample_iter(&Alphanumeric)
        .take(cfg.benchmark.value_length)
        .map(char::from)
        .collect();

    // -------------------------- Create work directory ----------------------
    std::fs::create_dir_all(&cfg.benchmark.work_dir).unwrap_or_else(|e| {
        panic!(
            "Failed to create work directory {}: {}",
            cfg.benchmark.work_dir, e
        );
    });

    // -------------------------- Init remote servers -------------------------
    if let Some(remote_script_config) = &cfg.benchmark.remote_script {
        let remote_script_config = remote_script_config.clone();
        let output = Command::new("ssh")
            .args([
                "-tt",
                &remote_script_config.remote_addr,
                "-p",
                &remote_script_config.remote_ssh_port,
                &format!(
                        r#"sudo python3 {}/tests/LevelDB/src/tests/scripts/leveldb/init.py -t {} -b {} -f {} -r {} -w {} -n {} -s {}"#,
                        remote_script_config.root_dir,
                        match &remote_script_config.experiment_type {
                            ExperimentType::Full => "Full",
                            ExperimentType::Checkpoint(_) => "Checkpoint",
                            ExperimentType::Lite(_, _) => "Lite",
                            ExperimentType::Ebpf(_, _) => "Ebpf",
                        },
                        remote_script_config.write_buffer_size,
                        cfg.benchmark.file_prefix,
                        remote_script_config.root_dir,
                        cfg.benchmark.work_dir,
                        match &remote_script_config.experiment_type {
                            ExperimentType::Lite(num_threads, _) => num_threads,
                            ExperimentType::Ebpf(num_threads, _) => num_threads,
                            _ => &0,
                        },
                        match &remote_script_config.experiment_type {
                            ExperimentType::Lite(_, memory_size) => memory_size,
                            ExperimentType::Ebpf(_, memory_size) => memory_size,
                            _ => "0G",
                        },
                    ),
            ])
            .output()
            .expect("Failed to init remote leveldb server");
        match output.status.code() {
            Some(0) => {
                println!("Initialized remote leveldb server: {:?}", output);
            }
            Some(_) => {
                panic!("Failed to init remote leveldb server: {:?}", output);
            }
            None => {
                panic!("Failed to init remote leveldb server: {:?}", output);
            }
        }
        sleep(Duration::from_secs(1)).await;
    };

    let pool = cfg.redis.create_pool(Some(Runtime::Tokio1)).unwrap();

    // -------------------------- Init the database ---------------------------
    let mut values = Vec::new();
    let mut stales = Vec::new(); // shared the same lock with values
    for _ in 0..cfg.benchmark.num_keys + 1 {
        values.push(Arc::new(Mutex::new(0 as usize)));
        stales.push(Arc::new(AtomicBool::new(false)));
    }

    for iter in 0..cfg.benchmark.inital_iter_count {
        println!("Initializing database for the {}th time", iter);
        let bar = ProgressBar::new(cfg.benchmark.num_keys as u64).with_prefix("Initializing");
        bar.set_style(
            ProgressStyle::with_template(
                "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
            )
            .unwrap(),
        );

        let mut handles = Vec::new();
        let start_time = Instant::now();

        for i in (1..cfg.benchmark.num_keys + 1).rev() {
            let iter_end_time = start_time + init_interval * ((cfg.benchmark.num_keys - i + 1) as u32);
            let pool = pool.clone();
            let i = i;
            let value = format!("{}_{}_{}", base_value, i, 0);
            let bar = bar.clone();
            
            let handle = tokio::spawn(async move {
                for conn in pool.iter() {  // Iterate through all connections
                    let mut conn = conn.get().await.unwrap();
                    update_conn_if_not_established!(conn, cfg.benchmark.timeout);
                    cmd("SET")
                        .arg(generate_key(i, cfg.benchmark.key_length))
                    .arg(&value)
                    .query::<String>(&mut conn.conn.as_mut().unwrap())
                    .unwrap();
                bar.inc(1);
            }});
            handles.push(handle);
            sleep_until(iter_end_time).await;
        }

        for handle in handles {
            handle.await.unwrap();
        }
        let end_time = Instant::now();
        bar.finish();
        println!("\nFinished initializing database");
        let elapsed = end_time - start_time;
        println!(
            "Initialization time: {:?} ms, rps: {:?}",
            elapsed.as_millis(),
            cfg.benchmark.num_keys as f64 / elapsed.as_secs_f64()
        );
    }

    // -------------------------- Generate requests ---------------------------
    let mut idx = Vec::new();
    match cfg.benchmark.key_distribution {
        KeyDistribution::Sequential => {
            for i in 0..num_requests {
                idx.push(i % cfg.benchmark.num_keys + 1);
            }
        }
        KeyDistribution::Zipf(alpha) => {
            let mut rng = rand::thread_rng();
            let zipf = zipf::ZipfDistribution::new(cfg.benchmark.num_keys, alpha).unwrap();
            for _ in 0..num_requests {
                idx.push(zipf.sample(&mut rng));
            }
        }
    }
    let mut is_write = Vec::new();
    {
        let mut rng = rand::thread_rng();
        for _ in 0..num_requests {
            is_write.push(rng.gen_bool(cfg.benchmark.write_ratio));
        }
    }

    let mut handles = Vec::new();
    let records = Arc::new(Mutex::new(vec![]));

    // -------------------------- Set up remote scripts -----------------------
    if let Some(remote_script_config) = &cfg.benchmark.remote_script {
        let now = SystemTime::now();
        let remote_script_config = remote_script_config.clone();
        let file_prefix = cfg.benchmark.file_prefix.clone();
        let work_dir = cfg.benchmark.work_dir.clone();
        let duration_since_epoch = now.duration_since(UNIX_EPOCH).expect("Time went backwards");
        let secs = duration_since_epoch.as_secs();
        let target_time = UNIX_EPOCH + Duration::from_secs(secs + 15);

        tokio::spawn(async move {
            let output = Command::new("ssh")
                .args([
                    "-tt",
                    &remote_script_config.remote_addr,
                    "-p",
                    &remote_script_config.remote_ssh_port,
                    &format!(
                        r#"sudo python3 {}/tests/LevelDB/src/tests/scripts/leveldb/start.py -c {} -s {} -t {} -l {} -f {} -b {} -r {} -w {} -u {} -i {}"#,
                        remote_script_config.root_dir,
                        remote_script_config.crash_time.as_secs(),
                        target_time.duration_since(UNIX_EPOCH).unwrap().as_nanos(),
                        match &remote_script_config.experiment_type {
                            ExperimentType::Full => "Full",
                            ExperimentType::Checkpoint(_) => "Checkpoint",
                            ExperimentType::Lite(_, _) => "Lite",
                            ExperimentType::Ebpf(_, _) => "Ebpf",
                        },
                        cfg.benchmark.test_duration.as_secs(),
                        file_prefix,
                        remote_script_config.write_buffer_size,
                        remote_script_config.root_dir,
                        work_dir,
                        remote_script_config.cpu_limit,
                        match &remote_script_config.experiment_type {
                            ExperimentType::Checkpoint(checkpoint_interval) => checkpoint_interval,
                            _ => &0,
                        },
                    ),
                ])
                .output()
                .expect("Failed to set up remote script");
            match output.status.code() {
                Some(0) => {
                    println!("Set up remote script: {:?}", output);
                }
                Some(_) => {
                    panic!("Failed to set up remote script: {:?}", output);
                }
                None => {
                    panic!("Failed to set up remote script: {:?}", output);
                }
            }
        });

        let duration_until_target_time = target_time
            .duration_since(SystemTime::now())
            .expect("Target time is in the past");
        sleep(duration_until_target_time).await;
    }

    // -------------------------- Benchmark -----------------------------------
    let bar = ProgressBar::new(num_requests as u64).with_prefix("Benchmarking");
    bar.set_style(
        ProgressStyle::with_template(
            "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
        )
        .unwrap(),
    );
    let start_time = Instant::now();
    for i in 0..num_requests {
        let iter_end_time = start_time + interval * (i as u32 + 1);
        let pool = pool.clone();
        let key = idx[i];
        let is_write = is_write[i];
        let value = values[idx[i]].clone();
        let stale = stales[idx[i]].clone();
        let base_value = base_value.clone();
        let records = Arc::clone(&records);
        let bar = bar.clone();
        let conn_info = cfg.redis.connection.as_ref().unwrap().clone();
        let handle = tokio::spawn(async move {
            let begin = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            let mut queries = Vec::new();
            let mut tries = 1;
            while tries <= cfg.benchmark.retry_count {
                let mut pool_conn: deadpool_redis::Connection;
                let mut redis_conn: RedisConnection;
                let mut redis_conn_mut: &mut RedisConnection;
                let stale = stale.clone();
                if cfg.benchmark.enable_connection_pool {
                    match pool.get().await {
                        Ok(conn) => {
                            pool_conn = conn;
                            redis_conn_mut = pool_conn.as_mut();
                        }
                        Err(_) => {
                            // eprintln!("\npool error i: {}, key: {}, error: {}\n", i, key, e);
                            let now_time = SystemTime::now()
                                .duration_since(UNIX_EPOCH)
                                .expect("Time went backwards");
                            queries.push(QueryRecord {
                                status: Status::Timeout,
                                request: now_time,
                                response: now_time,
                            });
                            tries += 1;
                            continue;
                        }
                    }
                } else {
                    redis_conn =
                        RedisConnection::new(redis::Client::open(conn_info.clone()).unwrap());
                    redis_conn_mut = &mut redis_conn;
                }
                let mut value_guard = value.lock().await;
                let mut old_suffix_expected = (*value_guard).clone();
                let query_record = do_query(
                    i,
                    &mut redis_conn_mut,
                    key,
                    &generate_key(key, cfg.benchmark.key_length),
                    &base_value,
                    &mut old_suffix_expected,
                    is_write,
                    cfg.benchmark.timeout,
                    stale,
                )
                .await;
                let finished = query_record.status != Status::Timeout;
                queries.push(query_record);
                *value_guard = old_suffix_expected;
                if finished {
                    break;
                }
                tries += 1;
                drop(value_guard);
            }
            let record = Record {
                i,
                key,
                begin,
                queries,
            };
            {
                let mut records_guard = records.lock().await;
                records_guard.push(record);
            }
            bar.inc(1);
            if key == cfg.benchmark.num_keys
                && cfg.benchmark.key_distribution == KeyDistribution::Sequential
            {
                println!("\nAll keys are covered, go back to key 1 again\n");
            }
        });
        handles.push(handle);
        sleep_until(iter_end_time).await;
    }
    let spawn_end_time = Instant::now();

    let mut alive_handles = Vec::new();
    println!("\nSpawn all requests. Trying to await for all requests to be finished\n");
    for handle in handles {
        if handle.is_finished() {
            handle.await.unwrap();
        } else {
            alive_handles.push(handle);
        }
    }
    println!("\n{} requests are still alive\n", alive_handles.len());
    if !alive_handles.is_empty() {
        sleep_until(
            start_time
                + cfg.benchmark.test_duration
                + cfg.benchmark.timeout * cfg.benchmark.retry_count as u32,
        )
        .await;
    }
    let mut wait_time = 30 / 5;
    while alive_handles.len() > 0 && wait_time > 0 {
        sleep(Duration::from_secs(5)).await;
        alive_handles.retain(|h| {
            if h.is_finished() {
                h.abort();
                false
            } else {
                true
            }
        });
        wait_time -= 1;
        println!("{} requests are still alive\n", alive_handles.len());
    }
    println!("\nAbort all unfinished requests\n");
    pool.close();
    drop(pool);
    for handle in alive_handles {
        if handle.is_finished() {
            handle.await.unwrap();
        } else {
            handle.abort();
            bar.inc(1);
        }
    }
    bar.finish();
    let end_time = Instant::now();

    println!("\nFinished benchmarking");
    let spawn_elapsed = spawn_end_time - start_time;
    let elapsed = end_time - start_time;
    println!(
        "Spawning time: {:?} ms, rps: {:?}",
        spawn_elapsed.as_millis(),
        num_requests as f64 / spawn_elapsed.as_secs_f64()
    );
    println!(
        "Serve time: {:?} ms, rps: {:?}",
        elapsed.as_millis(),
        num_requests as f64 / elapsed.as_secs_f64()
    );
    println!(
        "Number of unfinished timeout requests: {}",
        num_requests - (*records.lock().await).len()
    );

    {
        let records_guard = records.lock().await;
        let json = serde_json::to_string(&(*records_guard)).unwrap();
        std::fs::write(format!("{}.jsonl", cfg.benchmark.file_prefix), json).unwrap();
    }

    // -------------------------- Check correctness ---------------------------
    if cfg.benchmark.check_correctness {
        let pool = cfg.redis.create_pool(Some(Runtime::Tokio1)).unwrap();
        // BUG: Error occurred while creating a new object: Cannot assign requested address (os error 99) if some connections are aborted
        let mut handles = Vec::new();
        let bar = ProgressBar::new(cfg.benchmark.num_keys as u64).with_prefix("Validating");
        bar.set_style(
            ProgressStyle::with_template(
                "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
            )
            .unwrap(),
        );
        let start_time = Instant::now();
        for i in 1..cfg.benchmark.num_keys + 1 {
            let pool = pool.clone();
            let i = i; // Copy i into the closure
            let expected_value = format!("{}_{}_{}", base_value, i, values[i].lock().await);
            let bar = bar.clone();
            let handle = tokio::spawn(async move {
                let mut conn = pool.get().await.unwrap_or_else(|e| {
                    panic!("validating failed key: {}, error: {}", i, e);
                });
                update_conn_if_not_established!(conn, cfg.benchmark.timeout);
                let actual_value: Option<String> = cmd("GET")
                    .arg(generate_key(i, cfg.benchmark.key_length))
                    .query(&mut conn.conn.as_mut().unwrap())
                    .unwrap();
                match actual_value {
                    Some(actual_value) => {
                        if actual_value != expected_value {
                            eprintln!(
                                "\nERR! key: {}, expected value: {:?}, actual value: {:?}. If the actual value is newer than expected, it may be because a timeout occurred between the server sending the response and the client receiving it.\n",
                                i,
                                get_last_n_char(&expected_value, 10),
                                get_last_n_char(&actual_value, 10)
                            );
                        }
                    }
                    None => {
                        eprintln!(
                            "\nERR! key: {}, expected value: {:?}, no actual value\n",
                            i,
                            get_last_n_char(&expected_value, 10)
                        );
                    }
                }
                bar.inc(1);
            });
            handles.push(handle);
        }
        for handle in handles {
            handle.await.unwrap();
        }
        let end_time = Instant::now();
        bar.finish();
        println!("\nFinished validating correctness");
        let elapsed = end_time - start_time;
        println!(
            "Validation time: {:?} ms, rps: {:?}",
            elapsed.as_millis(),
            cfg.benchmark.num_keys as f64 / elapsed.as_secs_f64()
        );
    }

    // -------------------------- Copy results to remote server -------------
    if let Some(remote_script_config) = cfg.benchmark.remote_script {
        let file_prefix = cfg.benchmark.file_prefix.clone();
        let work_dir = cfg.benchmark.work_dir.clone();
        let remote_addr = remote_script_config.remote_addr.clone();
        let remote_ssh_port = remote_script_config.remote_ssh_port.clone();
        tokio::spawn(async move {
            let output = Command::new("scp")
                .args([
                    "-P",
                    &remote_ssh_port,
                    &format!(r#"{}.jsonl"#, file_prefix,),
                    &format!(r#"{}:{}"#, remote_addr, work_dir),
                ])
                .output()
                .expect("Failed to copy client log to remote server");
            match output.status.code() {
                Some(0) => {
                    println!("Copied client log to remote server: {:?}", output);
                }
                Some(_) => {
                    panic!("Failed to copy client log to remote server: {:?}", output);
                }
                None => {
                    panic!("Failed to copy client log to remote server: {:?}", output);
                }
            }
        });
    }
}
