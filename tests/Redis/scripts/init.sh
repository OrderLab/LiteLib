#!/bin/bash

set -x

sudo apt install -y maven redis-server
sudo systemctl stop redis-server
sudo systemctl disable redis-server
sudo sysctl vm.overcommit_memory=1
pip install psutil