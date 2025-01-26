# Sample-Hadoop-Cluster

A sample Hadoop cluster augmented with ZKFC and YARN, for MapReduce tasks.

The official Docker image of [Apache Hadoop](https://hub.docker.com/r/apache/hadoop) is based on CentOS 7, but as the EOL of CentOS 7 on June 30th, 2024, no new updates for CentOS will be made available. Time to deploy a Hadoop cluster in Docker on our own!

### Overview

In this experiment, we will set up a Hadoop cluster in Docker with 2 name nodes and 3 data nodes, along with ZKFC (Zookeeper Failover Controller) and YARN (Yet Another Resource Negotiator). 

### Use Guide

```sh
# Set up the lite
docker exec -it datanode1 bash
# Inside dn1
/code/tests/HDFS-datanode/docker/lite/lite-setup.sh
cd /code/tests/HDFS-datanode/src/build
cmake ..
make
./LiteHdfsDatanode
# After all the containers finish their boot-up:
docker exec -it namenode-active bash
# Inside NNA(name node active)
source /etc/profile
hadoop namenode -format
/usr/local/hadoop/sbin/start-all.sh
# restart the datanode (inside dn1)
hdfs --daemon stop datanode
hdfs --daemon start datanode
# Trigger the emergency
/code/tests/HDFS-datanode/src/build/Lite/lite_cli -t /tmp/LiteDatanode_data -p 60001 -m 1
/code/tests/HDFS-datanode/src/build/Lite/lite_cli -t /tmp/LiteDatanode_rpc -p 60001 -m 1
/code/tests/HDFS-datanode/src/build/Lite/lite_cli -t /tmp/LiteDatanode_nnrpc -p 60001 -m 1
```