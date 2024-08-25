# Generate protocpp file
protoc -I=./proto --cpp_out=./protocpp ./proto/*.proto
