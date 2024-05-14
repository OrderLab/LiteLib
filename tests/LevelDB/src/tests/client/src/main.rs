use async_mutex::Mutex;
use deadpool_redis::{
    redis::{cmd, pipe},
    Pool, Runtime,
};
use dotenvy::dotenv;
use duration_str::deserialize_duration;
use indicatif::{ProgressBar, ProgressStyle};
use rand::distributions::{Alphanumeric, Distribution};
use rand::Rng;
use std::process::Command;
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep, sleep_until, timeout, Duration, Instant};

#[derive(Debug, serde::Deserialize)]
enum ExperimentType {
    Full,
    Lite(usize, String),
}

#[derive(Debug, serde::Deserialize)]
struct RemoteScriptConfig {
    experiment_type: ExperimentType,
    remote_addr: String,
    monitor_file_path: String,
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
    #[serde(deserialize_with = "deserialize_duration")]
    timeout: Duration,
    retry_count: usize,
    file_path: String,
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

        if !cfg.benchmark.file_path.ends_with(".jsonl") {
            panic!("file_path must end with .jsonl");
        }

        Ok(cfg)
    }
}

#[derive(Debug, PartialEq, serde::Serialize)]
enum Status {
    Success,
    Miss,
    Timeout,
    Error,
    TransactionError,
}

#[derive(Debug, serde::Serialize)]
struct Record {
    i: usize,
    key: usize,
    begin: Duration,
    last_request_time: Duration,
    last_response_time: Duration,
    tries: usize,
    status: Status,
}

fn get_last_n_char(string: &str, n: usize) -> &str {
    let len = string.len();
    if len < n {
        return string;
    }
    &string[len - n..]
}

async fn do_set(
    i: usize,
    pool: Pool,
    key: usize,
    base_value: &str,
    old_suffix_expected: &mut usize,
) -> (Status, Duration, Duration) {
    let conn = match pool.get().await {
        Ok(conn) => conn,
        Err(_) => {
            // eprintln!("\ni: {}, key: {}, error: {}\n", i, key, e);
            let now_time = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            return (Status::Error, now_time, now_time);
        }
    };
    let mut conn_guard = conn.lock().await;
    let new_suffix_expected = *old_suffix_expected + 1;
    let new_value_expected = format!("{}_{}_{}", base_value, key, new_suffix_expected);
    let response_time: Duration;
    let request_time = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards");
    return match redis::cmd("SET")
        .arg(&key)
        .arg(&new_value_expected)
        .query_async::<redis::aio::Connection, String>(&mut *conn_guard)
        .await
    {
        Ok(_) => {
            *old_suffix_expected = new_suffix_expected;
            response_time = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            (Status::Success, request_time, response_time)
        }
        Err(_) => {
            // eprintln!("\ni: {}, key: {}, error: {}\n", i, key, e);
            response_time = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            (Status::Error, request_time, response_time)
        }
    };
}

async fn do_transaction(
    i: usize,
    pool: Pool,
    key: usize,
    base_value: &str,
    old_suffix_expected: &mut usize,
) -> (Status, Duration, Duration) {
    let conn = match pool.get().await {
        Ok(conn) => conn,
        Err(_) => {
            // eprintln!("\ni: {}, key: {}, error: {}\n", i, key, e);
            let now_time = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            return (Status::Error, now_time, now_time);
        }
    };
    let mut conn_guard = conn.lock().await;
    let new_suffix_expected = *old_suffix_expected + 1;
    let new_value_expected = format!("{}_{}_{}", base_value, key, new_suffix_expected);
    let old_value_expected = format!("{}_{}_{}", base_value, key, *old_suffix_expected);
    let response_time: Duration;
    let request_time = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards");
    let (old_value, new_value): (Option<String>, Option<String>) = match pipe()
        .atomic()
        .cmd("GET")
        .arg(&key)
        .cmd("SET")
        .arg(&key)
        .arg(&new_value_expected)
        .ignore()
        .cmd("GET")
        .arg(&key)
        .query_async(&mut *conn_guard)
        .await
    {
        Ok(result) => {
            response_time = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            result
        }
        Err(_) => {
            response_time = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            // println!("i: {}, key: {}, error: {}", i, key, e);
            return (Status::Error, request_time, response_time);
        }
    };
    let new_value = match new_value {
        Some(new_value) => new_value,
        None => {
            eprintln!("\ni: {}, key: {} can't get new value\n", i, key);
            return (Status::TransactionError, request_time, response_time);
        }
    };
    let old_value = match old_value {
        Some(old_value) => old_value,
        None => {
            eprintln!("\ni: {}, key: {} can't get old value\n", i, key);
            *old_suffix_expected = new_suffix_expected;
            return (Status::Miss, request_time, response_time);
        }
    };
    if new_value != new_value_expected || old_value != *old_value_expected {
        eprintln!("\ni: {}, key: {}, expected old value: {:?}, old value: {:?}, expected new value: {:?}, new value: {:?}\n", i, key, get_last_n_char(&old_value_expected, 10), get_last_n_char(&old_value, 10), get_last_n_char(&new_value_expected, 10), get_last_n_char(&new_value, 10));
        *old_suffix_expected = new_suffix_expected;
        (Status::TransactionError, request_time, response_time)
    } else {
        *old_suffix_expected = new_suffix_expected;
        (Status::Success, request_time, response_time)
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

    if let Some(remote_script_config) = &cfg.benchmark.remote_script {
        let output = Command::new("ssh")
            .args([
                "-tt",
                &remote_script_config.remote_addr,
                &match &remote_script_config.experiment_type {
                    ExperimentType::Full => {
                        r#"python3 /workspace/scripts/leveldb/init.py -t Full"#.to_string()
                    }
                    ExperimentType::Lite(num_threads, memory_size) => format!(
                        r#"python3 /workspace/scripts/leveldb/init.py -t Lite -n {} -s {}"#,
                        num_threads, memory_size,
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
        tokio::time::sleep(Duration::from_secs(1)).await;
    };

    let pool = cfg.redis.create_pool(Some(Runtime::Tokio1)).unwrap();

    // Initialize the database
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
    for i in (1..cfg.benchmark.num_keys + 1).rev() {
        let pool = pool.clone();
        let i = i; // Copy i into the closure
        let value = format!("{}_{}_{}", base_value, i, 0);
        let bar = bar.clone();
        let handle = tokio::spawn(async move {
            let conn = pool.get().await.unwrap_or_else(|e| {
                panic!("Initialize failed i: {}, error: {}", i, e);
            });
            let mut conn_guard = conn.lock().await;
            let _: () = cmd("SET")
                .arg(&i)
                .arg(&value)
                .query_async(&mut *conn_guard)
                .await
                .unwrap();
            bar.inc(1);
        });
        handles.push(handle);
    }
    for handle in handles {
        handle.await.unwrap();
    }
    bar.finish();
    println!("\nFinished initializing database");

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

    let mut handles = Vec::new();
    let records = Arc::new(Mutex::new(vec![]));
    let bar = ProgressBar::new(num_requests as u64).with_prefix("Benchmarking");
    bar.set_style(
        ProgressStyle::with_template(
            "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
        )
        .unwrap(),
    );

    if let Some(remote_script_config) = cfg.benchmark.remote_script {
        let now = SystemTime::now();
        let duration_since_epoch = now.duration_since(UNIX_EPOCH).expect("Time went backwards");
        let secs = duration_since_epoch.as_secs();
        let target_time = UNIX_EPOCH + Duration::from_secs(secs + 3);

        tokio::spawn(async move {
            let output = Command::new("ssh")
                .args([
                    "-tt",
                    &remote_script_config.remote_addr,
                    &format!(
                        r#"python3 /workspace/scripts/leveldb/start.py -c {} -s {} -t {} -l {} -f {}"#,
                        remote_script_config.crash_time.as_secs(),
                        target_time.duration_since(UNIX_EPOCH).unwrap().as_nanos(),
                        match &remote_script_config.experiment_type {
                            ExperimentType::Full => "Full",
                            ExperimentType::Lite(_, _) => "Lite",
                        },
                        cfg.benchmark.test_duration.as_secs(),
                        remote_script_config.monitor_file_path,
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
        tokio::time::sleep(duration_until_target_time).await;
    }

    let start_time = Instant::now();
    for i in 0..num_requests {
        let iter_end_time = start_time + interval * (i as u32 + 1);
        let pool = pool.clone();
        let key = idx[i];
        let value = values[idx[i]].clone();
        let base_value = base_value.clone();
        let records = Arc::clone(&records);
        let bar = bar.clone();
        let handle = tokio::spawn(async move {
            let begin = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            let mut last_request_time = begin;
            let mut last_response_time = begin;
            let mut tries = 1;
            let mut status = Status::Timeout;
            while tries <= cfg.benchmark.retry_count {
                let mut value_guard = value.lock().await;
                let mut old_suffix_expected = (*value_guard).clone();
                match timeout(
                    cfg.benchmark.timeout,
                    do_transaction(i, pool.clone(), key, &base_value, &mut old_suffix_expected),
                )
                .await
                {
                    Ok((result, request_time, response_time)) => {
                        last_request_time = request_time;
                        last_response_time = response_time;
                        if result != Status::Error {
                            status = result;
                            *value_guard = old_suffix_expected;
                            break;
                        }
                    }
                    Err(_) => {
                        // eprintln!("\ntimeout\n");
                    }
                }
                tries += 1;
                drop(value_guard);
            }
            if tries > cfg.benchmark.retry_count {
                status = Status::Timeout;
            }
            let record = Record {
                i,
                key,
                begin,
                last_request_time,
                last_response_time,
                tries,
                status,
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

    for handle in handles {
        handle.await.unwrap();
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

    {
        let records_guard = records.lock().await;
        let json = serde_json::to_string(&(*records_guard)).unwrap();
        std::fs::write(cfg.benchmark.file_path, json).unwrap();
    }

    // Check correctness
    sleep(Duration::from_secs(1)).await; // Wait for the last transaction to finish
    let mut handles = Vec::new();
    let bar = ProgressBar::new(cfg.benchmark.num_keys as u64).with_prefix("Validating");
    bar.set_style(
        ProgressStyle::with_template(
            "{prefix} [{elapsed_precise}] [{bar:40}] ({pos}/{len}, ETA {eta})",
        )
        .unwrap(),
    );
    for i in 1..cfg.benchmark.num_keys + 1 {
        let pool = pool.clone();
        let i = i; // Copy i into the closure
        let expected_value = format!("{}_{}_{}", base_value, i, values[i].lock().await);
        let bar = bar.clone();
        let handle = tokio::spawn(async move {
            let conn = pool.get().await.unwrap_or_else(|e| {
                panic!("validating failed key: {}, error: {}", i, e);
            });
            let mut conn_guard = conn.lock().await;
            let actual_value: Option<String> = cmd("GET")
                .arg(&i)
                .query_async(&mut *conn_guard)
                .await
                .unwrap();
            match actual_value {
                Some(actual_value) => {
                    if actual_value != expected_value {
                        eprintln!(
                            "\nERR! key: {}, expected value: {:?}, actual value: {:?}\n",
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
    bar.finish();
    println!("\nFinished validating correctness");
}
