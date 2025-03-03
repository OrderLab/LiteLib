apt install -y maven redis
systemctl stop redis-server
systemctl disable redis-server
sysctl vm.overcommit_memory=1