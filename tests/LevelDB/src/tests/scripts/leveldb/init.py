import os
import subprocess

os.system(r"rm redis.db -r")
os.system(r'pgrep "redis-leveldb" | xargs kill -9')

boot_command = ["/opt/redis-leveldb/redis-leveldb", "-P", "6379"]
process = subprocess.Popen(boot_command, start_new_session=True)
print(boot_command)
if process.poll() is not None:
    print(f"The process ended with return code {process.returncode}")
else:
    print("The process is still running")