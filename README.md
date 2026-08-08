# LiteLib

LiteLib is designed to contain the impact of software failures in stateful
applications and preserve useful service capacity during recovery, without the
resource cost of maintaining another full replica.

LiteLib builds and manages a compact replica that keeps only the bounded state
needed to support an application's core operations. For example, in the default
eBPF deployment, clients communicate directly with the full application while
an eBPF program asynchronously monitors state-changing requests and their
responses. If the application fails, the compact replica enters emergency mode,
serves supported operations, and logs updates. After the application recovers,
LiteLib replays those updates before returning to normal operation.

The repository includes the LiteLib library itself, along with integrations for
Memcached, LevelDB, Redis, MySQL, and HDFS DataNode.

## Artifact Evaluation

To reproduce the NSDI '27 paper's results, start with the
[artifact evaluation guide](docs/ae.md).

## Library Requirements

- CMake
- Boost 1.83 or newer
- libevent
- glog

The eBPF deployment also requires Clang, libbpf, and bpftool.

The development container in [`src/Dockerfile`](src/Dockerfile) installs the
base dependencies. The artifact setup scripts install the additional eBPF
tooling.

## Building the Library

From the repository root:

```bash
cmake -S src -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build -j"$(nproc)"
cmake --install build
```

This builds the static `Lite` library and the `lite_cli` mode-control utility,
then installs the library under `install/`.

## Using LiteLib in an Application

### 1. Link the library

To build LiteLib as part of another CMake project:

```cmake
find_package(Boost 1.83 REQUIRED COMPONENTS system thread)
find_package(Libevent REQUIRED)
find_package(glog REQUIRED)

add_subdirectory("/path/to/LiteLib/src" LiteLib-build)

target_link_libraries(my_lite_replica PRIVATE
  Lite
  libevent::core
  Boost::system
  Boost::thread
  glog::glog
)
```

If LiteLib was installed first, use its exported CMake target:

```cmake
find_package(Boost 1.83 REQUIRED COMPONENTS system thread)
find_package(Libevent REQUIRED)
find_package(glog REQUIRED)
find_package(Lite CONFIG REQUIRED)

target_link_libraries(my_lite_replica PRIVATE
  lite::Lite
  libevent::core
  Boost::system
  Boost::thread
  glog::glog
)
```

Set `Lite_DIR` to `/path/to/LiteLib/install/cmake` if CMake cannot locate the
installed package. Include `<lite.hpp>` for an in-tree build or
`<Lite/lite.hpp>` for an installed build.

### 2. Implement the application adapter

LiteLib is protocol- and application-independent. An integration supplies:

- Request and response types with `Serialize()` and `Deserialize()` methods.
- A cache-entry type with `ToRequest()`, used to reconstruct replay requests.
- Per-connection state.
- An application adapter that implements the callbacks defined by
  [`IsApplication`](src/include/concept.hpp):
  - `Match()` associates a backend response with pending requests.
  - `NormalUpdate()` extracts compact-replica state during normal operation.
  - `EmergencyServe()` implements supported operations during a failure.
  - `HandleReplayResponse()` processes backend responses as LiteLib replays
    logged updates.
  - The mode-transition and emergency-connection hooks perform any
    application-specific setup.

The Memcached integration provides a complete example:

- [protocol and application types](tests/Memcached/src/lite-version/include)
- [callback implementation](tests/Memcached/src/lite-version/src/service.cc)
- [`LiteServer` construction](tests/Memcached/src/lite-version/main.cc)
- [CMake integration](tests/Memcached/src/lite-version/CMakeLists.txt)

### 3. Start LiteLib in eBPF mode

The default eBPF deployment keeps LiteLib off the normal request path. Start the
full application first, then instantiate `lite::LiteServer` with the adapter
types, compact-state capacity, recovery endpoint, replay settings, and
control-plane socket:

```cpp
const char* service_port = "6379";
std::string recovery_backend_address = "";
std::string recovery_backend_socket = "/tmp/application.sock";

lite::LiteServer<Application, Request, Response, ConnectionInfo,
                 CacheKey, CacheEntry>
    server(worker_threads, max_items, application, recovery_backend_address,
           recovery_backend_socket, 1000ms, expected_replay_rps, 0.9, 1,
           "/tmp/lite_control_plane.sock");

server.Run(service_port);
```

Clients continue to connect directly to the full application on `service_port`.
LiteLib attaches its eBPF monitoring path to that service and asynchronously
updates the compact replica. The recovery endpoint tells LiteLib how to
reconnect for replay after the full application restarts; leave its address
empty to use a Unix-domain socket. The LitePlugin uses the control-plane socket
to transfer client and listener sockets during takeover.

### 4. Switch operating modes

LiteLib can enter emergency mode automatically when it detects that all
connections to the backend have closed unexpectedly. In the eBPF deployment,
the LitePlugin transfers the surviving client and listener sockets through the
control-plane socket so LiteLib can begin serving them.

For controlled tests or configurations that use named-pipe control, `lite_cli`
can request the same transitions manually. Message `1` enters emergency mode.
Message `0` exits emergency mode and starts replay against the recovered
backend:

```bash
./build/lite_cli -t /tmp/lite -p <backend-port-or-socket> -m 1
./build/lite_cli -t /tmp/lite -p <backend-port-or-socket> -m 0
```

## Repository Layout

* `src`: LiteLib source code
* `tests`: application integrations and evaluation workloads
* `scripts`: artifact setup, experiment, processing, plotting, and cleanup scripts
* `docs`: documentation
