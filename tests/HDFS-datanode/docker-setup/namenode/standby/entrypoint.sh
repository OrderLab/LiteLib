#!/bin/bash

rm -rf /workspace/data

# Install necessary packages
apt-get update
apt-get install -y ssh openjdk-8-jdk vim htop net-tools iputils-ping tar wget curl psmisc

# Enable non-password ssh login within cluster
service ssh restart
ssh-keygen -t rsa -b 4096 -f ~/.ssh/id_rsa -N ""
cat /root/.ssh/id_rsa.pub >>/root/.ssh/authorized_keys
cat /public/hosts >>/etc/hosts

# Install Hadoop 3.3.6
wget https://downloads.apache.org/hadoop/common/hadoop-3.3.6/hadoop-3.3.6.tar.gz -P /tmp
tar -xzf /tmp/hadoop-3.3.6.tar.gz -C /usr/local
mv /usr/local/hadoop-3.3.6 /usr/local/hadoop

# Configure Hadoop path related environment variables
cat /public/hadoop-path >>/etc/profile
source /etc/profile
echo "export JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java))))" >>/usr/local/hadoop/etc/hadoop/hadoop-env.sh
echo "export HADOOP_LOG_DIR=/workspace/data/logs" >>/usr/local/hadoop/etc/hadoop/hadoop-env.sh
cat /public/workers >/usr/local/hadoop/etc/hadoop/workers

cp /public/config/* /usr/local/hadoop/etc/hadoop

sed -i '2iHDFS_DATANODE_USER=root\nHDFS_DATANODE_SECURE_USER=hdfs\nHDFS_NAMENODE_USER=root\nHDFS_SECONDARYNAMENODE_USER=root' /usr/local/hadoop/sbin/start-dfs.sh
sed -i '2iHDFS_DATANODE_USER=root\nHDFS_DATANODE_SECURE_USER=hdfs\nHDFS_NAMENODE_USER=root\nHDFS_SECONDARYNAMENODE_USER=root' /usr/local/hadoop/sbin/stop-dfs.sh
sed -i '2iYARN_RESOURCEMANAGER_USER=root\nHADOOP_SECURE_DN_USER=yarn\nYARN_NODEMANAGER_USER=root' /usr/local/hadoop/sbin/start-yarn.sh
sed -i '2iYARN_RESOURCEMANAGER_USER=root\nHADOOP_SECURE_DN_USER=yarn\nYARN_NODEMANAGER_USER=root' /usr/local/hadoop/sbin/stop-yarn.sh

scp -rq /usr/local/hadoop dn1:/usr/local
scp -rq /usr/local/hadoop dn2:/usr/local
scp -rq /usr/local/hadoop dn3:/usr/local

tail -f /dev/null
