update_property() {
    local file=$1
    local property=$2
    local value=$3

    # if grep -q "<name>$property</name>" $file; then
    #     # Property exists, update its value
    #     sed -i "/<name>$property<\/name>/!b;n;c<value>$value</value>" $file
    # else
        # Property doesn't exist, add it
        sed -i "/<\/configuration>/ i <property><name>$property</name><value>$value</value></property>" $file
    # fi
}

# Update hdfs-site.xml
update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.namenode.rpc-address" "dn1:11111"