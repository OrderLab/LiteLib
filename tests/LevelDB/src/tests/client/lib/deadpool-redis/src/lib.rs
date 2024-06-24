#![doc = include_str!("../README.md")]
#![cfg_attr(docsrs, feature(doc_cfg))]
#![deny(
    nonstandard_style,
    rust_2018_idioms,
    rustdoc::broken_intra_doc_links,
    rustdoc::private_intra_doc_links
)]
#![forbid(non_ascii_idents, unsafe_code)]
#![warn(
    deprecated_in_future,
    missing_copy_implementations,
    missing_debug_implementations,
    missing_docs,
    unreachable_pub,
    unused_import_braces,
    unused_labels,
    unused_lifetimes,
    unused_qualifications,
    unused_results
)]
#![allow(clippy::uninlined_format_args)]

#[cfg(feature = "cluster")]
pub mod cluster;
mod config;

use std::{
    ops::{Deref, DerefMut},
    sync::atomic::{AtomicUsize, Ordering},
};

use tokio::time::{sleep, Duration};

use deadpool::{async_trait, managed};
use redis::{Client, Connection as RedisConnection, IntoConnectionInfo, RedisError, RedisResult};

pub use redis;

pub use self::config::{Config, ConfigError, ConnectionAddr, ConnectionInfo, RedisConnectionInfo};

pub use deadpool::managed::reexports::*;
deadpool::managed_reexports!("redis", Manager, Connection, RedisError, ConfigError);

/// Type alias for using [`deadpool::managed::RecycleResult`] with [`redis`].
type RecycleResult = managed::RecycleResult<RedisError>;

/// Wrapper around [`redis::aio::Connection`].
///
/// This structure implements [`redis::aio::ConnectionLike`] and can therefore
/// be used just like a regular [`redis::aio::Connection`].
#[allow(missing_debug_implementations)] // `redis::aio::Connection: !Debug`
pub struct Connection {
    conn: Object,
}

impl Connection {
    /// Takes this [`Connection`] from its [`Pool`] permanently.
    ///
    /// This reduces the size of the [`Pool`].
    #[must_use]
    pub fn take(this: Self) -> RedisConnection {
        Object::take(this.conn)
    }
}

impl From<Object> for Connection {
    fn from(conn: Object) -> Self {
        Self { conn }
    }
}

impl Deref for Connection {
    type Target = RedisConnection;

    fn deref(&self) -> &RedisConnection {
        &self.conn
    }
}

impl DerefMut for Connection {
    fn deref_mut(&mut self) -> &mut RedisConnection {
        &mut self.conn
    }
}

impl AsRef<RedisConnection> for Connection {
    fn as_ref(&self) -> &RedisConnection {
        &self.conn
    }
}

impl AsMut<RedisConnection> for Connection {
    fn as_mut(&mut self) -> &mut RedisConnection {
        &mut self.conn
    }
}

/// [`Manager`] for creating and recycling [`redis`] connections.
///
/// [`Manager`]: managed::Manager
#[derive(Debug)]
pub struct Manager {
    client: Client,
    ping_number: AtomicUsize,
}

impl Manager {
    /// Creates a new [`Manager`] from the given `params`.
    ///
    /// # Errors
    ///
    /// If establishing a new [`Client`] fails.
    pub fn new<T: IntoConnectionInfo>(params: T) -> RedisResult<Self> {
        Ok(Self {
            client: Client::open(params)?,
            ping_number: AtomicUsize::new(0),
        })
    }
}

#[async_trait]
impl managed::Manager for Manager {
    type Type = RedisConnection;
    type Error = RedisError;

    async fn create(&self) -> Result<RedisConnection, RedisError> {
        let conn = self.client.get_connection()?;
        Ok(conn)
    }

    async fn recycle(&self, conn: &mut RedisConnection, _: &Metrics) -> RecycleResult {
        let ping_number = self.ping_number.fetch_add(1, Ordering::Relaxed).to_string();
        let n = redis::cmd("PING").arg(&ping_number).query::<String>(conn)?;
        if n == ping_number {
            Ok(())
        } else {
            sleep(Duration::from_millis(100)).await; // backoff
            println!("Broken connection or the async bug of redis-rs was triggered");
            Err(managed::RecycleError::StaticMessage(
                "Invalid PING response",
            ))
        }
    }
}
