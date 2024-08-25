#!/bin/bash

rm -rf /workspace/data

apt-get update
apt-get install -y ssh openjdk-8-jdk vim htop net-tools iputils-ping tar wget curl

service ssh restart
cat /public/hosts >/etc/hosts

cat /public/hadoop-path >>/etc/profile
source /etc/profile

tail -f /dev/null
