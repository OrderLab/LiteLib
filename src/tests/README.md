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

## Memcached ASCII framing

The `ascii_*` tests exercise the actual proxy, eBPF-adapter and shared-memory
Packet types. They cover every two-part split and one-byte delivery, empty
input, embedded CRLF/NUL bytes, zero-length values, coalesced packets, more
than 255 VALUE lines, malformed/overflow lengths, exact command names and
sticky rejection. Seeded randomized tests add 1,000 binary frames per variant.
The proxy/eBPF service tests use the ordinary in-process cache library (not
kernel eBPF) to check incomplete-frame rejection, exact values/flags, failed
storage replies, add/replace key boundaries, replay serialization and queue
matching. The embedded tests use the actual service and shared-memory cache
for incomplete/truncated rejection, binary values, GET, add/replace keys and
replay serialization (including legacy trailing-space flags). That fixture
keeps logging disabled and reclaims its cache by unmapping the private,
already-unlinked segment; it does not qualify cache deletion/destruction,
the embedded full-backend update ABI,
interprocess concurrency or handover.

Run only these tests with `ctest --test-dir build/cache-tests -R '^ascii_' \
--output-on-failure`. Libevent is also required. The sanitizer option applies
to all targets.

All three adapters share `tests/Memcached/src/ascii_protocol.hpp`. Values are
length-delimited, and GET responses complete only after END. Unsupported
commands/options, including `noreply` (incompatible with the existing pending
request queue), are explicitly rejected. Socket handlers close malformed
connections instead of retaining a poisoned decoder. Passive eBPF update
decoders remain failed until their connection is retired; kernel capture and
handover require separate runtime validation. Existing emergency add/replace
upsert behavior and expiration handling are unchanged.
