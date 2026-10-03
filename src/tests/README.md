# Embedded Redis lifetime regression tests

These tests use isolated shared-memory segments with unique names, unlinked
immediately after opening. They do not launch Redis, bind ports or use the
application's shared-memory name.

```bash
cmake -S src/tests -B build/embedded-tests \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEMBEDDED_SANITIZERS=ON
cmake --build build/embedded-tests -j2
ctest --test-dir build/embedded-tests --output-on-failure
```

The cache tests cover single-worker eviction, the 65,536-entry production
capacity, remapping at another address, deletion, destruction, concurrent
emergency publication, replay-entry uniqueness, no-log writes and callers
already holding a transaction lock.

The Redis test compiles the real adapter and exercises concurrent HSET and
HGETALL on one hash, verifies every field and the single final replay entry,
and checks EXEC for an already-open transaction without recursive locking.
Starting a new Redis transaction is outside this focused test.

Emergency cache mutation now holds the existing interprocess transaction lock
through replay-log publication. Hash operations hold that same lock across
lookup, shared-map access and publication; a shared pointer preserves lifetime,
but does not make its mutable map thread-safe. Delete unlinks/accounting before
erasure, and MapEntry remains the sole owner of state and LRU-node allocations.
Cache destruction clears the map before its allocators are destroyed.

Capacity units and normal single-worker workload settings are unchanged.
Sanitizers do not individually instrument allocations inside the shared-memory
segment, so allocator sanity, exact returned values and replay invariants are
also checked. Local tests do not replace application recovery validation.
