import os
import re
import numpy as np
import matplotlib.pyplot as plt

def get_latency_data():
    data = {}
    pattern = r'(full|lite)-(\d+\.\d+)\.process\.txt'

    for filename in os.listdir('.'):
        match = re.match(pattern, filename)
        if match:
            type_name = match.group(1)
            write_ratio = float(match.group(2))

            if type_name not in data:
                data[type_name] = {
                    'write_ratios': [],
                    'before_client_avg': [],
                    'before_client_p95': [],
                    'before_server_avg': [],
                    'before_server_p95': [],
                }

            with open(filename, 'r') as f:
                content = f.read()

                # Extract metrics using regex
                before_client_avg = float(re.search(r'before crash avg latency_client_avg (\d+\.\d+)', content).group(1))
                before_client_p95 = float(re.search(r'before crash avg latency_client_p95 (\d+\.\d+)', content).group(1))
                before_server_avg = float(re.search(r'before crash avg latency_server_avg (\d+\.\d+)', content).group(1))
                before_server_p95 = float(re.search(r'before crash avg latency_server_p95 (\d+\.\d+)', content).group(1))

                data[type_name]['write_ratios'].append(write_ratio)
                data[type_name]['before_client_avg'].append(before_client_avg)
                data[type_name]['before_client_p95'].append(before_client_p95)
                data[type_name]['before_server_avg'].append(before_server_avg)
                data[type_name]['before_server_p95'].append(before_server_p95)

    # Sort data by write ratios
    for type_name in data:
        indices = np.argsort(data[type_name]['write_ratios'])
        for key in data[type_name]:
            data[type_name][key] = np.array(data[type_name][key])[indices]

    return data

def plot_latency():
    data = get_latency_data()

    plt.figure(figsize=(10, 6))

    # Create twin axes
    ax1 = plt.gca()
    ax2 = ax1.twinx()

    # Plot bars for latency
    bar_width = 0.35
    x = np.arange(len(data['lite']['write_ratios']))

    ax1.bar(x - bar_width/2, data['full']['before_client_avg'],
            bar_width, label='Vanilla', color='tab:green', alpha=0.5)
    ax1.bar(x + bar_width/2, data['lite']['before_client_avg'],
            bar_width, label='LiteSys', color='tab:orange', alpha=0.5)

    # Plot overhead curve (lite vs full)
    overhead = data['lite']['before_client_avg'] / data['full']['before_client_avg'] - 1
    print(overhead)
    ax2.plot(x, overhead, '--', color='tab:red', alpha=0.7, label='Lite/Full Overhead')
    ax2.set_ylim(0, 1)

    # Set labels and legend
    ax1.set_xlabel('Write Ratio (%)')
    ax1.set_ylabel('Average Latency (ms)')
    ax2.set_ylabel('Latency Overhead')

    # Set x-axis ticks to show actual write ratios
    ax1.set_xticks(x)
    ax1.set_xticklabels([ratio * 100 for ratio in data['lite']['write_ratios']])

    # Combine legends
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left')
    plt.grid(True)
    plt.savefig('leveldb_avg_client_latency_overhead.png')
    plt.savefig('leveldb_avg_client_latency_overhead.pdf')
    plt.close()

if __name__ == "__main__":
    plot_latency()
