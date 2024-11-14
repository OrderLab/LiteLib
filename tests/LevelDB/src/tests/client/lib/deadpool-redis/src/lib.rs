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

use std::ops::{Deref, DerefMut};

use tokio::time::{sleep, Duration};

use deadpool::{async_trait, managed};
use redis::{Client, Connection as RedisConnection, IntoConnectionInfo, RedisError, RedisResult, ConnectionLike};

pub use redis;

pub use self::config::{Config, ConfigError, ConnectionAddr, ConnectionInfo, RedisConnectionInfo};

pub use deadpool::managed::reexports::*;
deadpool::managed_reexports!("redis", Manager, Connection, RedisError, ConfigError);

/// Type alias for using [`deadpool::managed::RecycleResult`] with [`redis`].
type RecycleResult = managed::RecycleResult<RedisError>;

/// Custom connection wrapper that includes connection status
#[allow(missing_debug_implementations)]
pub struct CustomRedisConnection {
    /// The underlying Redis client
    pub client: Client,
    /// The underlying Redis connection
    pub conn: Option<RedisConnection>,
    /// Indicates whether the connection is currently valid and usable
    pub is_valid: bool,
}

impl CustomRedisConnection {
    fn new(client: Client) -> Self {
        Self {
            client,
            conn: None,
            is_valid: true,
        }
    }
}

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
    pub fn take(this: Self) -> CustomRedisConnection {
        Object::take(this.conn)
    }
}

impl From<Object> for Connection {
    fn from(conn: Object) -> Self {
        Self { conn }
    }
}

impl Deref for Connection {
    type Target = CustomRedisConnection;

    fn deref(&self) -> &CustomRedisConnection {
        &self.conn
    }
}

impl DerefMut for Connection {
    fn deref_mut(&mut self) -> &mut CustomRedisConnection {
        &mut self.conn
    }
}

impl AsRef<CustomRedisConnection> for Connection {
    fn as_ref(&self) -> &CustomRedisConnection {
        &self.conn
    }
}

impl AsMut<CustomRedisConnection> for Connection {
    fn as_mut(&mut self) -> &mut CustomRedisConnection {
        &mut self.conn
    }
}

/// [`Manager`] for creating and recycling [`redis`] connections.
///
/// [`Manager`]: managed::Manager
#[derive(Debug)]
pub struct Manager {
    client: Client
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
        })
    }
}

#[async_trait]
impl managed::Manager for Manager {
    type Type = CustomRedisConnection;
    type Error = RedisError;

    async fn create(&self) -> Result<CustomRedisConnection, RedisError> {
        Ok(CustomRedisConnection::new(self.client.clone()))
    }

    async fn recycle(&self, conn: &mut CustomRedisConnection, _: &Metrics) -> RecycleResult {
        if conn.is_valid && conn.conn.as_ref().unwrap().is_open() {
            Ok(())
        } else {
            sleep(Duration::from_millis(100)).await;
            match conn.is_valid {
                true => println!("Broken connection"),
                false => println!("Timeout connection"),
            }
            Err(managed::RecycleError::StaticMessage(
                "Connection is closed",
            ))
        }
    }
}
