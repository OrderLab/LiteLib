## Dev Docker

```sh
docker build -f Dockerfile --build-arg NUM_JOBS=`nproc --all` --tag=lite-sys .
```

## Usage

### Internal Library

* CMakeLists

```
add_subdirectory(`pwd`)
```

### External Library

* Build

```sh
cmake --build . --target install
```

* CMakeLists

```
#set(CMAKE_PREFIX_PATH "/path/to/someLibrary/install")
find_package(Lite CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE lite::Lite)
```

## GLOG

```sh
GLOG_stderrthreshold=0 GLOG_logtostderr=1 ./lite
```