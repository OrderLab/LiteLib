use deadpool_redis::{
    redis::{cmd, pipe},
    Pool, Runtime,
};
use dotenvy::dotenv;
use duration_str::deserialize_duration;
use rand::distributions::{Alphanumeric, Distribution};
use rand::Rng;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep_until, timeout, Duration, Instant};

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
        Err(e) => {
            println!("i: {}, key: {}, error: {}", i, key, e);
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
        Err(e) => {
            println!("i: {}, key: {}, error: {}", i, key, e);
            return Status::Error;
        }
    };
    if old_value.is_none() {
        println!("i: {}, key: {} miss", i, key);
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

    let pool = cfg.redis.create_pool(Some(Runtime::Tokio1)).unwrap();

    // Initialize the database
    let mut handles = Vec::new();
    for i in (1..cfg.benchmark.num_keys + 1).rev() {
        let pool = pool.clone();
        let i = i; // Copy i into the closure
        let value = base_value.clone();
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
        });
        handles.push(handle);
    }
    for handle in handles {
        handle.await.unwrap();
    }

    let mut idx = Vec::new();
    let mut rng = rand::thread_rng();
    let zipf =
        zipf::ZipfDistribution::new(cfg.benchmark.num_keys, cfg.benchmark.zipf_alpha).unwrap();
    for _ in 0..num_requests {
        idx.push(zipf.sample(&mut rng));
    }

    let mut handles = Vec::new();
    let records = Arc::new(Mutex::new(vec![]));

    let start_time = Instant::now();
    for i in 0..num_requests {
        let iter_end_time = start_time + interval * (i as u32 + 1);
        let pool = pool.clone();
        let key = idx[i];
        let value = base_value.clone() + &idx[i].to_string();
        let records = Arc::clone(&records);
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
        });
        handles.push(handle);
        sleep_until(iter_end_time).await;
    }
    let end_time = Instant::now();
    let elapsed = end_time - start_time;
    println!(
        "Spawning time: {:?}, actual rps: {:?}",
        elapsed,
        num_requests as f64 / elapsed.as_secs_f64()
    );

    for handle in handles {
        handle.await.unwrap();
    }

    {
        let records = records.lock().unwrap();
        let json = serde_json::to_string(&(*records)).unwrap();
        std::fs::write(cfg.benchmark.file_path, json).unwrap();
    }
}
