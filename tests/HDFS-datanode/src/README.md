# Generate protocpp file

protoc -I=$SRC_DIR --cpp_out=$DST_DIR $SRC_DIR/addressbook.proto

/code/tests/HDFS-datanode/src/build/Lite/lite_cli -t /tmp/LiteDatanode_data -p 60001 -m 1