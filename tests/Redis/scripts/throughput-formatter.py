import csv
import re
import argparse
from datetime import datetime

def parse_log(log_file, csv_file):
    with open(log_file, 'r') as file:
        lines = file.readlines()

    run_started = False
    data = []
    start_time = None

    for line in lines:
        if "DBWrapper: report latency" in line:
            run_started = True
            continue

        if run_started:
            match = re.match(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}:\d{3}) \d+ sec: \d+ operations; \d+ current ops/sec; est completion in \d+ (seconds?|minutes?) \[(.*)\]', line)
            if match and 'INSERT' not in line:
                timestamp_str = match.group(1)
                timestamp = datetime.strptime(timestamp_str, '%Y-%m-%d %H:%M:%S:%f')
                if start_time is None:
                    start_time = timestamp
                elapsed_time = int((timestamp - start_time).total_seconds())
                
                metrics_str = match.group(3)
                metrics = metrics_str.split('] [')
                read, update, read_failed, update_failed, read_missed = 0, 0, 0, 0, 0
                for metric in metrics:
                    if metric.startswith('READ-FAILED:'):
                        read_failed = int(re.search(r'Count=(\d+)', metric).group(1))
                    elif metric.startswith('UPDATE-FAILED:'):
                        update_failed = int(re.search(r'Count=(\d+)', metric).group(1))
                    elif metric.startswith('READ-MISSED:'):
                        read_missed = int(re.search(r'Count=(\d+)', metric).group(1))
                    elif metric.startswith('READ:'):
                        read = int(re.search(r'Count=(\d+)', metric).group(1))
                    elif metric.startswith('UPDATE:'):
                        update = int(re.search(r'Count=(\d+)', metric).group(1))

                data.append([elapsed_time, read, update, read_failed, update_failed, read_missed])

    with open(csv_file, 'w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['timestamp', 'read', 'update', 'read_failed', 'update_failed', 'read_missed'])
        writer.writerows(data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Format throughput logs.')
    parser.add_argument('log_file', type=str, help='Throughput log file')
    parser.add_argument('csv_file', type=str, default='throughput.csv', help='Output CSV file')
    args = parser.parse_args()

    parse_log(args.log_file, args.csv_file)
    
    # python throughput-formatter.py logs/benchmark.log data/throughput.csv