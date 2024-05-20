## Redis (Lite version)

### Build

```sh
# Working directory @ cascade/tests/Redis/src/lite-version
docker build -t redis-build .
docker run -d --rm -v .:/workspace -v "$(pwd)/../../../../src":/workspace/Lite --name redis-build redis-build

# Check if the building is done (Optional)
docker logs redis-build >& redis-build.log
```

The container stops and removes itself after building lite-Redis. The executable of lite-Redis is now built as `Redis/lite-version/build/Lite/redis-lite`