import psutil
import redis

def get_process(name, port):
    for proc in psutil.process_iter(['name', 'connections']):
        if proc.info['name'] == name:
            for conn in proc.info['connections']:
                if conn.laddr.port == port:
                    return proc
    return None

def get_usage(process):
    try:
        cpu_usage = process.cpu_percent()
        memory_usage = process.memory_info().rss
    except Exception:
        cpu_usage = 0
        memory_usage = 0
    return cpu_usage, memory_usage

def get_throughput(address, port):
    try:
        throughput = redis.Redis(host=address, port=port).info()['total_commands_processed']
    except Exception:
        throughput = 0
    return throughput