## Summary

Source code of LiteSys for HDFS datanode.

## Setup

In this setup, the HDFS cluster is deployed on 4 nodes:

- Node 0: NameNode + Client
- Node 1: DataNode + LiteSys
- Node 2: Vanilla DataNode
- Node 3: Vanilla DataNode

### Setup the nodes on bare metal

1. Install the following packages on all nodes:
   ```sh
   apt-get update && apt-get install -y ssh openjdk-11-jdk tar wget cmake
   ```
2. set up public key authentication between the nodes.
3. Install and configure HDFS on Node 0
   ```sh
   # Download and extract Hadoop
   wget https://downloads.apache.org/hadoop/common/hadoop-3.3.6/hadoop-3.3.6.tar.gz -P /tmp
   tar -xzf /tmp/hadoop-3.3.6.tar.gz -C /usr/local
   mv /usr/local/hadoop-3.3.6 /usr/local/hadoop
   # Configure HDFS
   echo "export JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java))))" >>/usr/local/hadoop/etc/hadoop/hadoop-env.sh
   echo "export HADOOP_LOG_DIR=<your log dir>" >>/usr/local/hadoop/etc/hadoop/hadoop-env.sh
   sed -i '2iHDFS_DATANODE_USER=root\nHDFS_DATANODE_SECURE_USER=hdfs\nHDFS_NAMENODE_USER=root\nHDFS_SECONDARYNAMENODE_USER=root' /usr/local/hadoop/sbin/start-dfs.sh
   sed -i '2iHDFS_DATANODE_USER=root\nHDFS_DATANODE_SECURE_USER=hdfs\nHDFS_NAMENODE_USER=root\nHDFS_SECONDARYNAMENODE_USER=root' /usr/local/hadoop/sbin/stop-dfs.sh
   # Copy the configuration files
   cp config/hdfs-site.xml /usr/local/hadoop/etc/hadoop/hdfs-site.xml
   cp config/core-site.xml /usr/local/hadoop/etc/hadoop/core-site.xml
   cp config/workers /usr/local/hadoop/etc/hadoop/workers
   ```
4. Copy the Hadoop binaries from Node 0 to Node 1, Node 2, and Node3
   ```sh
   scp -r /usr/local/hadoop node1:/usr/local/
   scp -r /usr/local/hadoop node2:/usr/local/
   scp -r /usr/local/hadoop node3:/usr/local/
   ```
5. Add paths to `.bashrc` for each node
   ```sh
   cat config/hadoop-path >> ~/.bashrc
   source ~/.bashrc
   ```
6. On Node 1, build LiteSys and modify the hadoop configuration
   ```sh
   # Under bare-setup
   ./setup-lite.sh # Install dependencies
   cd ../src &&  mkdir -p build && cd build && cmake .. && make -j4 # Build LiteSys
   ./update-config.sh # Modify the hadoop configuration
   ```
7. Start the HDFS cluster and test the setup
   ```sh
   # On Node 1, start LiteSys
   ./LiteHdfsDatanode
   # On Node 0, format namenode and start the cluster
   hdfs namenode -format
   /usr/local/hadoop/sbin/start-dfs.sh
   # On Node 1, restart the datanode
   hdfs --daemon stop datanode
   hdfs --daemon start datanode
   # Trigger the emergency, under the build directory
   # The `-m 1` flag is used to trigger the emergency mode, `-m 0` is used to exit the emergency mode.
   ./Lite/lite_cli -t /tmp/LiteDatanode_data -p 60001 -m 1
   ./Lite/lite_cli -t /tmp/LiteDatanode_rpc -p 60001 -m 1
   ./Lite/lite_cli -t /tmp/LiteDatanode_nnrpc -p 60001 -m 1
   ```
   Note: currently the datanodeuuid, namespceid, and clusterid are hardcoded in the source code, so those field in tests/HDFS-datanode/src/src/service.cc should be change accordingly and LiteSys datanode should be recompiled each time the namenode is re-formated, which changes the version ID and cluster ID.
