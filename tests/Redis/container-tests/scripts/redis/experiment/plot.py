import pandas as pd
import argparse
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser()
parser.add_argument('--mode', choices=['replica', 'lite'], default='replica', help='Select mode: replica or lite')
args = parser.parse_args()

data = pd.read_csv('redis_monitoring.csv')

fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(10, 10))

if args.mode == 'replica':
    ax1.plot(data['Timestamp'], data['Full CPU Usage'], label='Full CPU Usage')
    ax1.plot(data['Timestamp'], data['Replica CPU Usage'], label='Replica CPU Usage')
    ax1.set_ylabel('CPU Usage')

    ax2.plot(data['Timestamp'], data['Full Memory Usage'], label='Full Memory Usage')
    ax2.plot(data['Timestamp'], data['Replica Memory Usage'], label='Replica Memory Usage')
    ax2.set_ylabel('Memory Usage')

    ax3.plot(data['Timestamp'], data['Full Throughput'], label='Full Throughput')
    ax3.plot(data['Timestamp'], data['Replica Throughput'], label='Replica Throughput')
    ax3.set_xlabel('Timestamp')
    ax3.set_ylabel('Throughput')
else:
    lite_dat = pd.read_csv('lite_tp.csv')
    ax1.plot(data['Timestamp'], data['Full CPU Usage'], label='Full CPU Usage')
    ax1.plot(data['Timestamp'], data['Lite CPU Usage'], label='Lite CPU Usage')
    
    ax2.plot(data['Timestamp'], data['Full Memory Usage'], label='Full Memory Usage')
    ax2.plot(data['Timestamp'], data['Lite Memory Usage'], label='Lite Memory Usage')
    ax2.set_ylabel('Memory Usage')

    ax3.plot(data['Timestamp'], data['Full Throughput'], label='Full Throughput')
    ax3.plot(lite_dat['Timestamp'], lite_dat['Lite Throughput'], label='Lite Throughput')
    ax3.set_xlabel('Timestamp')
    ax3.set_ylabel('Throughput')

ax1.set_title('CPU Usage')
ax1.legend()
ax2.set_title('Memory Usage')
ax2.legend()
ax3.set_title('Throughput')
ax3.legend()

plt.savefig('result.png')
plt.close()
