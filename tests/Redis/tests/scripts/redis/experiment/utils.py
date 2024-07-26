import psutil
import redis
import subprocess


def get_process(name, port):
    for proc in psutil.process_iter(['name']):
        if proc.info['name'] == name:
            try:
                for conn in proc.connections(kind='inet'):
                    if conn.laddr.port == port:
                        return proc
            except psutil.AccessDenied:
                continue
    return None


def get_usage(process):
    try:
        cpu_usage = process.cpu_percent()
        memory_usage = process.memory_info().rss
    except Exception:
        cpu_usage, memory_usage = 0 , 0
    return cpu_usage, memory_usage


def cmd_done_cnt(address, port):
    try:
        throughput = redis.Redis(host=address, port=port).info()['total_commands_processed']
    except Exception:
        throughput = 0
    return throughput


def StartBackgroundProcess(boot_command):
    print(boot_command)
    process = subprocess.Popen(boot_command, start_new_session=True)
    if process.poll() is not None:
        print(f"The process ended with return code {process.returncode}")
        exit(1)
    else:
        print("The process is still running")

def IsProcessRunning(process_name):
    for proc in psutil.process_iter(['name']):
        if proc.info['name'] == process_name:
            return True
    return False