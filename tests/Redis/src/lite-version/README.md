## Redis (Lite version)

### Build

```sh
# Working directory @ cascade/tests/Redis/src/lite-version
docker build -t redis-build .
docker run -d --rm -v .:/workspace -v "$(pwd)/../../../../src":/workspace/Lite --name redis-build redis-build

# Check if the building env is setup (Optional)
docker logs redis-build >& redis-build.log

# Enter CLI of redis-build
docker exec -it redis-build bash

# Inside the container, working directory: /workspace
mkdir build && cd build
cmake ..
make
```

The executable of lite-Redis is now built in `Redis/lite-version/build`