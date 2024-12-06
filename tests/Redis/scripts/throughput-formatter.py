import csv
import re

def parse_log_file(log_file):
    data = []
    with open(log_file, 'r') as file:
        for line in file:
            match = re.search(r'\[RUN #(\d+) (\d+)%\s*,\s*(\d+)\s*secs\].*avg:\s*(\d+)\) ops/sec.*\),  ([\d.]+).*', line)
            if match:
                time = int(match.group(3))
                throughput = int(match.group(4))
                latency = float(match.group(5))
                data.append((time, throughput, latency))
    return data

def write_csv(data, output_file):
    with open(output_file, 'w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['Time', 'Throughput', 'Latency'])
        writer.writerows(data)

if __name__ == "__main__":
    log_file = 'logs/benchmark.log'
    output_file = 'data/throughput.csv'
    data = parse_log_file(log_file)
    write_csv(data, output_file)