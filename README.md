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