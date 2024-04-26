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
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep_until, timeout, Duration, Instant};

#[derive(Debug, serde::Deserialize)]
enum ExperimentType {
    Full,
    Lite(usize, String),
}

#[derive(Debug, serde::Deserialize)]
struct RemoteScriptConfig {
    experiment_type: ExperimentType,
    remote_addr: String,
    #[serde(deserialize_with = "deserialize_duration")]
    crash_time: Duration,
}

#[derive(Debug, serde::Deserialize)]
struct BenchmarkConfig {
    num_keys: usize,
    value_length: usize,
    #[serde(deserialize_with = "deserialize_duration")]
    test_duration: Duration,
    rps: usize,
    zipf_alpha: f64,
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
    end: Duration,
    tries: usize,
    status: Status,
}

async fn do_transaction(i: usize, pool: Pool, key: usize, value: String) -> Status {
    // TODO: change value to reference
    let mut conn = match pool.get().await {
        Ok(conn) => conn,
        Err(_) => {
            // println!("i: {}, key: {}, error: {}", i, key, e);
            return Status::Error;
        }
    };
    // BUG: https://docs.rs/deadpool-redis/0.14.0/deadpool_redis/struct.Manager.html#method.recycle has PING and UNWATCH command, which redis-leveldb does not support
    let (old_value, new_value): (Option<String>, Option<String>) = match pipe()
        .atomic()
        .cmd("GET")
        .arg(&key)
        .cmd("SET")
        .arg(&key)
        .arg(&value)
        .ignore()
        .cmd("GET")
        .arg(&key)
        .query_async(&mut conn)
        .await
    {
        Ok(result) => result,
        Err(_) => {
            // println!("i: {}, key: {}, error: {}", i, key, e);
            return Status::Error;
        }
    };
    if old_value.is_none() {
        // println!("i: {}, key: {} miss", i, key);
        return Status::Miss;
    }
    if new_value != Some(value) {
        println!("i: {}, key: {}, value: {:?}", i, key, new_value);
        Status::TransactionError
    } else {
        Status::Success
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
    for i in (1..cfg.benchmark.num_keys + 1).rev() {
        let pool = pool.clone();
        let i = i; // Copy i into the closure
        let value = base_value.clone();
        let bar = bar.clone();
        let handle = tokio::spawn(async move {
            let mut conn = pool.get().await.unwrap_or_else(|e| {
                panic!("Initialize failed i: {}, error: {}", i, e);
            });
            let _: () = cmd("SET")
                .arg(&i)
                .arg(&value)
                .query_async(&mut conn)
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
    let mut rng = rand::thread_rng();
    let zipf =
        zipf::ZipfDistribution::new(cfg.benchmark.num_keys, cfg.benchmark.zipf_alpha).unwrap();
    for _ in 0..num_requests {
        idx.push(zipf.sample(&mut rng));
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
                        r#"python3 /workspace/scripts/leveldb/start.py -c {} -s {} -t {}"#,
                        remote_script_config.crash_time.as_secs(),
                        target_time.duration_since(UNIX_EPOCH).unwrap().as_nanos(),
                        match &remote_script_config.experiment_type {
                            ExperimentType::Full => "Full",
                            ExperimentType::Lite(_, _) => "Lite",
                        }
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
        let value = base_value.clone() + &idx[i].to_string();
        let records = Arc::clone(&records);
        let bar = bar.clone();
        let handle = tokio::spawn(async move {
            let begin = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            let mut tries = 1;
            let mut status = Status::Timeout;
            while tries <= cfg.benchmark.retry_count {
                match timeout(
                    cfg.benchmark.timeout,
                    do_transaction(i, pool.clone(), key, value.to_string()),
                )
                .await
                {
                    Ok(result) => {
                        if result != Status::Error {
                            status = result;
                            break;
                        }
                    }
                    Err(_) => {
                        // println!("timeout");
                    }
                }
                tries += 1;
            }
            if tries > cfg.benchmark.retry_count {
                status = Status::Timeout;
            }
            let end = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("Time went backwards");
            let record = Record {
                i,
                key,
                begin,
                end,
                tries,
                status,
            };
            {
                let mut records = records.lock().unwrap();
                records.push(record);
            }
            bar.inc(1);
        });
        handles.push(handle);
        sleep_until(iter_end_time).await;
    }
    let end_time = Instant::now();
    let elapsed = end_time - start_time;

    for handle in handles {
        handle.await.unwrap();
    }
    bar.finish();

    println!("\nFinished benchmarking");
    println!(
        "Spawning time: {:?}, actual rps: {:?}",
        elapsed,
        num_requests as f64 / elapsed.as_secs_f64()
    );

    {
        let records = records.lock().unwrap();
        let json = serde_json::to_string(&(*records)).unwrap();
        std::fs::write(cfg.benchmark.file_path, json).unwrap();
    }
}
