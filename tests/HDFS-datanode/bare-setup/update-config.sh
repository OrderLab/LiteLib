#!/bin/bash

update_property() {
    local file=$1
    local property=$2
    local value=$3
    sed -i "/<\/configuration>/ i <property><name>$property</name><value>$value</value></property>" $file
}

update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.namenode.rpc-address" "node1:18020"
update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.datanode.address" "node1:19866"
update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.datanode.ipc.address" "node1:19867"