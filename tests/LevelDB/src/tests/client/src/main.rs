use async_mutex::Mutex;
use deadpool_redis::{
    redis::{cmd, Connection as RedisConnection},
    Runtime,
};
use dotenvy::dotenv;
use duration_str::deserialize_duration;
use indicatif::{ProgressBar, ProgressStyle};
use rand::distributions::{Alphanumeric, Distribution};
use rand::Rng;
use std::process::Command;
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep, sleep_until, Duration, Instant};

#[derive(Debug, Clone, serde::Deserialize)]
enum ExperimentType {
    Full,
    Lite(usize, String),
}

#[derive(Debug, Clone, serde::Deserialize)]
struct RemoteScriptConfig {
    experiment_type: ExperimentType,
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
    value_length: usize,
    #[serde(deserialize_with = "deserialize_duration")]
    test_duration: Duration,
    rps: usize,
    key_distribution: KeyDistribution,
    write_ratio: f64,
    #[serde(deserialize_with = "deserialize_duration")]
    timeout: Duration,
    retry_count: usize,
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

fn get_last_n_char(string: &str, n: usize) -> &str {
    let len = string.len();
    if len < n {
        return string;
    }
    &string[len - n..]
}

async fn do_query(
    i: usize,
    conn: &mut RedisConnection,
    key: usize,
    base_value: &str,
    old_suffix_expected: &mut usize,
    is_write: bool,
) -> QueryRecord {
    let new_suffix_expected = *old_suffix_expected + is_write as usize;
    let new_value_expected = format!("{}_{}_{}", base_value, key, new_suffix_expected);
    let request_time = SystemTime::now();
    let result = match is_write {
        true => match cmd("SET").arg(&key).arg(&new_value_expected).query::<String>(conn) {
            Ok(_) => Ok(Some(new_value_expected.clone())),
            Err(e) => Err(e),
        },
        false => cmd("GET").arg(&key).query::<Option<String>>(&mut *conn),
    };
    let response_time = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards");
    let request_time = request_time
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards");
    match result {
        Ok(new_value) => {
            let mut status = Status::Success;
            match new_value {
                Some(new_value) => {
                    if new_value != new_value_expected {
                        eprintln!(
                            "\ni: {}, key: {}, expected value: {:?}, actual value: {:?}\n",
                            i,
                            key,
                            get_last_n_char(&new_value_expected, 10),
                            get_last_n_char(&new_value, 10)
                        );
                        status = Status::Error;
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
        Err(e) => {
            // println!("request error i: {}, key: {}, error: {:?}", i, key, e);
            QueryRecord {
                status: if e.detail() != Some("flow control enabled") { Status::Timeout } else { Status::Error },
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
    let interval = Duration::from_secs_f64(1.0 / cfg.benchmark.rps as f64);
    let base_value: String = rand::thread_rng()
        .sample_iter(&Alphanumeric)
        .take(cfg.benchmark.value_length)
        .map(char::from)
        .collect();

    // -------------------------- Init remote servers -------------------------
    if let Some(remote_script_config) = &cfg.benchmark.remote_script {
        let remote_script_config = remote_script_config.clone();
        let output = Command::new("ssh")
            .args([
                "-tt",
                &remote_script_config.remote_addr,
                "-p",
                &remote_script_config.remote_ssh_port,
                &match &remote_script_config.experiment_type {
                    ExperimentType::Full => format! {
                        r#"python3 /workspace/scripts/leveldb/init.py -t Full -b {} -f {}"#,
                        remote_script_config.write_buffer_size,
                        cfg.benchmark.file_prefix
                    },
                    ExperimentType::Lite(num_threads, memory_size) => format!(
                        r#"python3 /workspace/scripts/leveldb/init.py -t Lite -n {} -s {} -b {} -f {}"#,
                        num_threads, memory_size, remote_script_config.write_buffer_size,
                        cfg.benchmark.file_prefix
                    ),
                },
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
    let bar = ProgressBar::new(cfg.benchmark.num_keys as u64).with_prefix("Initializing");
    bar.set_style(
        ProgressStyle::with_template(
            "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
        )
        .unwrap(),
    );
    let mut handles = Vec::new();
    let mut values = Vec::new();
    for _ in 0..cfg.benchmark.num_keys + 1 {
        values.push(Arc::new(Mutex::new(0 as usize)));
    }
    let start_time = Instant::now();
    for i in (1..cfg.benchmark.num_keys + 1).rev() {
        let pool = pool.clone();
        let i = i; // Copy i into the closure
        let value = format!("{}_{}_{}", base_value, i, 0);
        let bar = bar.clone();
        let handle = tokio::spawn(async move {
            let mut conn = pool.get().await.unwrap_or_else(|e| {
                panic!("Initialize failed i: {}, error: {}", i, e);
            });
            cmd("SET")
                .arg(&i)
                .arg(&value)
                .query::<String>(&mut conn)
                .unwrap();
            bar.inc(1);
        });
        handles.push(handle);
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
    let bar = ProgressBar::new(num_requests as u64).with_prefix("Benchmarking");
    bar.set_style(
        ProgressStyle::with_template(
            "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
        )
        .unwrap(),
    );

    // -------------------------- Set up remote scripts -----------------------
    if let Some(remote_script_config) = &cfg.benchmark.remote_script {
        let now = SystemTime::now();
        let remote_script_config = remote_script_config.clone();
        let file_prefix = cfg.benchmark.file_prefix.clone();
        let duration_since_epoch = now.duration_since(UNIX_EPOCH).expect("Time went backwards");
        let secs = duration_since_epoch.as_secs();
        let target_time = UNIX_EPOCH + Duration::from_secs(secs + 3);

        tokio::spawn(async move {
            let output = Command::new("ssh")
                .args([
                    "-tt",
                    &remote_script_config.remote_addr,
                    "-p",
                    &remote_script_config.remote_ssh_port,
                    &format!(
                        r#"python3 /workspace/scripts/leveldb/start.py -c {} -s {} -t {} -l {} -f {} -b {}"#,
                        remote_script_config.crash_time.as_secs(),
                        target_time.duration_since(UNIX_EPOCH).unwrap().as_nanos(),
                        match &remote_script_config.experiment_type {
                            ExperimentType::Full => "Full",
                            ExperimentType::Lite(_, _) => "Lite",
                        },
                        cfg.benchmark.test_duration.as_secs(),
                        file_prefix,
                        remote_script_config.write_buffer_size,
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
            .duration_since(now)
            .expect("Target time is in the past");
        sleep(duration_until_target_time).await;
    }

    // -------------------------- Benchmark -----------------------------------
    let start_time = Instant::now();
    for i in 0..num_requests {
        let iter_end_time = start_time + interval * (i as u32 + 1);
        let pool = pool.clone();
        let key = idx[i];
        let is_write = is_write[i];
        let value = values[idx[i]].clone();
        let base_value = base_value.clone();
        let records = Arc::clone(&records);
        let bar = bar.clone();
        let handle = tokio::spawn(async move {
            let begin = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            let mut queries = Vec::new();
            let mut tries = 1;
            while tries <= cfg.benchmark.retry_count {
                match pool.get().await {
                    Ok(mut conn) => {
                        let mut value_guard = value.lock().await;
                        conn.set_read_timeout(Some(cfg.benchmark.timeout))
                            .expect("set_read_timeout failed");
                        conn.set_write_timeout(Some(cfg.benchmark.timeout))
                            .expect("set_write_timeout failed");
                        let mut old_suffix_expected = (*value_guard).clone();
                        let query_record = do_query(
                            i,
                            &mut conn,
                            key,
                            &base_value,
                            &mut old_suffix_expected,
                            is_write,
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
                    }
                };
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
            let actual_value: Option<String> = cmd("GET").arg(&i).query(&mut conn).unwrap();
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


    // -------------------------- Copy results from remote server -------------
    if let Some(remote_script_config) = cfg.benchmark.remote_script {
        let file_prefix = cfg.benchmark.file_prefix.clone();
        let remote_addr = remote_script_config.remote_addr.clone();
        let remote_ssh_port = remote_script_config.remote_ssh_port.clone();
        tokio::spawn(async move {
            let output = Command::new("scp")
                .args([
                    "-P",
                    &remote_ssh_port,
                    &format!(
                        r#"{}:/workspace/client/monitor.{}.jsonl"#,
                        remote_addr,
                        file_prefix,
                    ),
                    ".",
                ])
                .output()
                .expect("Failed to copy monitor.jsonl from remote server");
            match output.status.code() {
                Some(0) => {
                    println!("Copied monitor.jsonl from remote server: {:?}", output);
                }
                Some(_) => {
                    panic!("Failed to copy monitor.jsonl from remote server: {:?}", output);
                }
                None => {
                    panic!("Failed to copy monitor.jsonl from remote server: {:?}", output);
                }
            }
        });
        let file_prefix = cfg.benchmark.file_prefix.clone();
        let remote_addr = remote_script_config.remote_addr.clone();
        let remote_ssh_port = remote_script_config.remote_ssh_port.clone();
        tokio::spawn(async move {
            let output = Command::new("scp")
                .args([
                    "-P",
                    &remote_ssh_port,
                    &format!(
                        r#"{}:/workspace/client/{}.log"#,
                        remote_addr,
                        file_prefix,
                    ),
                    ".",
                ])
                .output()
                .expect("Failed to copy log from remote server");
            match output.status.code() {
                Some(0) => {
                    println!("Copied log from remote server: {:?}", output);
                }
                Some(_) => {
                    panic!("Failed to copy log from remote server: {:?}", output);
                }
                None => {
                    panic!("Failed to copy log from remote server: {:?}", output);
                }
            }
        });
    }
}
