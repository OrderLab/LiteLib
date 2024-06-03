import argparse
from datetime import datetime

parser = argparse.ArgumentParser(description="Latency Calculator")
# tcpdump -q -r ./full.pcap > full.pcap.txt
# tcpdump -q -r ./lite.pcap > lite.pcap.txt
parser.add_argument("-f", "--pcap_file", type=str, help="The decoded pcap file")
parser.add_argument(
    "-p", "--server_port", type=str, help="The server port", default="redis"
)
parser.add_argument(
    "-i", "--server_ip", type=str, help="The server ip", default="172.21.0.2"
)
parser.add_argument(
    "-t",
    "--threshold",
    type=int,
    help="The threshold for TCP transmission (value length)",
)

# Parse the arguments
args = parser.parse_args()

print("Assume all requests and responses are not separated to multiple packets")
print(
    "Latency = resp_n - req_1 (req_1, req_2 ... req_n, resp_1, resp_2 ... resp_n are requests and response that are larger than the threshold, and sum(resp) > 2 * threshold)"
)
print(f"Pcap file: {args.pcap_file}")
print(f"Server IP: {args.server_ip}")
print(f"Server port: {args.server_port}")
print(f"Threshold: {args.threshold}")

latencies = []
requests = {}
responses = {}

with open(args.pcap_file, "r") as f:
    lines = f.readlines()
for line in lines:
    parts = line.split()
    time_obj = datetime.strptime(parts[0], "%H:%M:%S.%f")
    time = (
        time_obj.hour * 3600000
        + time_obj.minute * 60000
        + time_obj.second * 1000
        + time_obj.microsecond / 1000
    )
    src_parts = parts[4].split(".")
    src_ip, src_port = ".".join(src_parts[:-1]), src_parts[-1]
    dst_parts = parts[6].split(".")
    dst_ip, dst_port = ".".join(dst_parts[:-1]), dst_parts[-1].split(":")[0]
    tcp_size = int(parts[-1])
    if src_port == args.server_port and src_ip == args.server_ip and tcp_size > args.threshold:
        if dst_port not in requests:
            continue
        if dst_port in responses:
            old_resp = responses[dst_port]
            responses[dst_port] = (time, old_resp[1] + tcp_size)
        else:
            responses[dst_port] = (time, tcp_size)
    if dst_port == args.server_port and dst_ip == args.server_ip and tcp_size > args.threshold:
        if src_port in responses:
            resp = responses[src_port]
            if resp[1] > 2 * args.threshold:
              latencies.append(resp[0] - requests[src_port])
            del requests[src_port]
        if src_port not in requests:
            requests[src_port] = time
print(f"Average latency: {sum(latencies)/len(latencies)}")
