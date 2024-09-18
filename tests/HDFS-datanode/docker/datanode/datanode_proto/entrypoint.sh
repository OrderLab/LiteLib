#!/bin/bash

update_property() {
    local file=$1
    local property=$2
    local value=$3
    sed -i "/<\/configuration>/ i <property><name>$property</name><value>$value</value></property>" $file
}

rm -rf /workspace/data

apt-get update
apt-get install -y ssh openjdk-8-jdk vim htop net-tools iputils-ping tar wget curl cmake

service ssh restart
cat /public/hosts >/etc/hosts

cat /public/hadoop-path >>/etc/profile
source /etc/profile

if [ "$WITH_LITE" = "true" ] ; then
    echo "Building with Lite"
    /workspace/build_lite.sh
    update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.namenode.rpc-address" "dn1:18020"
    update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.datanode.address" "dn1:19866"
    update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.datanode.ipc.address" "dn1:19867"
else
    echo "Datanode without Lite"
fi

tail -f /dev/null

./lite_cli -t /tmp/LiteDatanode_data -p 60001 -m 1
./lite_cli -t /tmp/LiteDatanode_rpc -p 60001 -m 1
./lite_cli -t /tmp/LiteDatanode_nnrpc -p 60001 -m 1