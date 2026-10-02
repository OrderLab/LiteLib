# Cache publication and control-message regression tests

Build and run the tests without starting any application workload:

```bash
cmake -S src/tests -B build/cache-tests -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLITE_CACHE_SANITIZERS=ON
cmake --build build/cache-tests -j2
ctest --test-dir build/cache-tests --output-on-failure
```

Boost 1.87, glog, and a C++20 compiler are required, as for the library.

The concurrent tests exercise the same emergency `Cache::Set` path used by
LiteMemcached: writers must not replace/free a dirty entry between publishing
the cache state and linking its replay log. Every cached key must retain
exactly one replay entry with the final value. The serial, explicit-transaction,
non-logging, normal-mode, byte-eviction and concurrent-delete cases cover the
related API behavior.

Emergency mutations now hold the existing exclusive cache transaction lock
through replay-log publication. Callers already holding `TransactionLock()`
must continue passing `in_transaction=true`, as before. Normal-mode add and
replace retain their existing concurrency. Delete holds exclusive access
while removing both the map entry and LRU node, and saves the byte count before
the map erases its owning cache state.

The control-pipe test reproduces an early nonblocking-reader wakeup with the
original fragmented wire layout. The writer now publishes the unchanged layout
in one atomic write of at most `PIPE_BUF` bytes, retries interrupted writes,
and rejects oversized messages with `EMSGSIZE`. Tests cover round trips, full
pipe rejection without a partial frame, maximum-size messages, and two
concurrent writers with a nonblocking reader. The sanitizer option applies to
both test executables.

These tests establish memory safety for the exercised interleavings, not a
full application recovery or performance qualification.
