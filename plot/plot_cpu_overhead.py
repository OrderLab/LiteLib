import json
import matplotlib.pyplot as plt
import argparse
import matplotlib
import numpy as np

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

parser = argparse.ArgumentParser()
parser.add_argument("filenames", nargs="+", help="The path to the CPU overhead file(s)")
parser.add_argument("-o", "--output", help="The path to the output file")
args = parser.parse_args()

def plot_bar_label(bars, labels, indices=None):
    # If indices is provided, only label those specific bars
    if indices is None:
        # Filter out labels where value is 0 or empty
        filtered_labels = [l if l and float(l) != 0 else "" for l in labels]
        plt.bar_label(bars,
                     labels=filtered_labels,
                     padding=0, fontsize=8, label_type='center', rotation=90,
                     bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
    else:
        # Create a list of empty strings with the same length as the bar container
        all_labels = [""] * len(bars)
        # Fill in only the specified indices with their corresponding labels
        for i, idx in enumerate(indices):
            if idx < len(labels) and labels[idx] and float(labels[idx]) != 0:
                all_labels[idx] = labels[idx]
        plt.bar_label(bars,
                     labels=all_labels,
                     padding=0, fontsize=8, label_type='center', rotation=90,
                     bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))

with open(args.filenames[0], 'r') as f:
    data = json.load(f)

# Prepare data for plotting
systems = list(data.keys())


for system in systems:
    # Get baseline value - use 'full' if exists, otherwise use replica's full
    full_value = data[system].get('full', data[system]['replica']['master'] if 'replica' in data[system] else None)
    if full_value is None:
        raise ValueError(f"No baseline value found for system {system}")
    
    # Calculate normalized values for each component
    proxy_total = 0
    if 'proxy' in data[system]:
        proxy_total = (data[system]['proxy'].get('full', 0) + 
                      data[system]['proxy'].get('lite', 0)) / full_value
    
    checkpoint = data[system].get('checkpoint', 0) / full_value if 'checkpoint' in data[system] else 0
    
    replica_total = 0
    if 'replica' in data[system]:
        replica_total = (data[system]['replica'].get('master', 0) + 
                        data[system]['replica'].get('other', 0)) / full_value
    
    embedded = data[system].get('embedded', 0) / full_value if 'embedded' in data[system] else 0


# Now calculate the values array based on the sorted systems
values = []
for system in systems:
    # Get baseline value - use 'full' if exists, otherwise use replica's full
    full_value = data[system].get('full', data[system]['replica']['master'] if 'replica' in data[system] else None)
    if full_value is None:
        raise ValueError(f"No baseline value found for system {system}")
        
    system_values = [
        data[system]['proxy']['full'] / full_value if 'proxy' in data[system] else 0,
        data[system]['proxy']['lite'] / full_value if 'proxy' in data[system] else 0,
        data[system]['ebpf']['full'] / full_value if 'ebpf' in data[system] else 0,
        data[system]['ebpf']['lite'] / full_value if 'ebpf' in data[system] else 0,
        data[system]['checkpoint'] / full_value if 'checkpoint' in data[system] else 0,
        data[system]['replica']['master'] / full_value if 'replica' in data[system] else 0,
        data[system]['replica']['other'] / full_value if 'replica' in data[system] else 0,
        data[system]['embedded'] / full_value if 'embedded' in data[system] else 0,
        data[system]['ndb(client)']['node1'] / full_value if 'ndb(client)' in data[system] else 0,
        data[system]['ndb(client)']['node2'] / full_value if 'ndb(client)' in data[system] else 0,
        data[system]['ndb(client)']['other'] / full_value if 'ndb(client)' in data[system] else 0,
        data[system]['ndb(proxy)']['node1'] / full_value if 'ndb(proxy)' in data[system] else 0,
        data[system]['ndb(proxy)']['node2'] / full_value if 'ndb(proxy)' in data[system] else 0,
        data[system]['ndb(proxy)']['other'] / full_value if 'ndb(proxy)' in data[system] else 0
    ]
    values.append(system_values)

# Create the plot
plt.figure(figsize=(8, 3.0))

bar_interval = 0.25
bar_width = bar_interval - 0.05
gap_between_groups = 0.75

def compute_bar_values_and_order(system):
    full_value = data[system].get('full', data[system]['replica']['master'] if 'replica' in data[system] else None)
    if full_value is None:
        raise ValueError(f"No baseline value found for system {system}")
    bar_values = {}
    if 'full' in data[system]:
        bar_values['baseline'] = 1.0
    if 'proxy' in data[system]:
        proxy_total = data[system]['proxy'].get('full', 0) / full_value + data[system]['proxy'].get('lite', 0) / full_value
        if proxy_total > 0:
            bar_values['proxy'] = proxy_total
    if 'ebpf' in data[system]:
        ebpf_total = data[system]['ebpf'].get('full', 0) / full_value + data[system]['ebpf'].get('lite', 0) / full_value
        if ebpf_total > 0:
            bar_values['ebpf'] = ebpf_total
    if 'checkpoint' in data[system] and data[system]['checkpoint'] > 0:
        bar_values['checkpoint'] = data[system]['checkpoint'] / full_value
    if 'replica' in data[system]:
        replica_total = data[system]['replica'].get('master', 0) / full_value + data[system]['replica'].get('other', 0) / full_value
        if replica_total > 0:
            bar_values['replica'] = replica_total
    if 'embedded' in data[system] and data[system]['embedded'] > 0:
        bar_values['embedded'] = data[system]['embedded'] / full_value
    for ndb_key in ['ndb', 'ndb(client)', 'ndb(proxy)']:
        if ndb_key in data[system]:
            ndb_data = data[system][ndb_key]
            ndb_total = ndb_data.get('node1', 0) / full_value + ndb_data.get('node2', 0) / full_value + ndb_data.get('other', 0) / full_value
            if ndb_total > 0:
                bar_values[ndb_key] = ndb_total
    ndb_like = [k for k in bar_values.keys() if k in ('ndb', 'ndb(client)', 'ndb(proxy)')]
    sorted_bars = sorted(
        [k for k in bar_values.keys() if k not in ['baseline'] and k not in ndb_like],
        key=lambda k: bar_values[k]
    )
    for k in ['ndb', 'ndb(client)', 'ndb(proxy)']:
        if k in bar_values:
            sorted_bars.append(k)
    return bar_values, sorted_bars

num_bars_per_system = [len(compute_bar_values_and_order(s)[1]) for s in systems]
x = np.zeros(len(systems))
for j in range(len(systems) - 1):
    n = num_bars_per_system[j]
    x[j + 1] = x[j] + gap_between_groups + (n - 1) * bar_interval if n > 0 else x[j] + gap_between_groups

# Create empty lists to store handles for the legend
legend_handles = []
legend_labels = []

# For each system, determine the order of bars based on their heights
for i, system in enumerate(systems):
    bar_values, sorted_bars = compute_bar_values_and_order(system)
    full_value = data[system].get('full', data[system]['replica']['master'] if 'replica' in data[system] else None)
    
    # Plot baseline bar (always at position -1)
    if 'baseline' in bar_values:
        baseline_bar = plt.bar(x[i] - bar_interval, 1.0, bar_width,
                              color='tab:green', alpha=0.5, hatch='..')
        # Add to legend only once
        if i == 0:
            legend_handles.append(baseline_bar)
            legend_labels.append('Vanilla Full Version')
        
        if 'full' in data[system] and data[system]['full'] > 0:
            plt.bar_label(baseline_bar, labels=[f'{data[system]["full"]:.1f}'],
                         padding=0, fontsize=8, label_type='center', rotation=90,
                         bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
    
    # Plot other bars in order of increasing height
    for j, bar_type in enumerate(sorted_bars):
        position = x[i] + (j * bar_interval)
        
        if bar_type == 'proxy':
            # Plot proxy full
            proxy_full_val = data[system]['proxy'].get('full', 0) / full_value
            if proxy_full_val > 0:
                proxy_full_bar = plt.bar(position, proxy_full_val, bar_width,
                                       color='tab:green', alpha=0.5, hatch='..')
                # Add to legend only once
                if 'Vanilla Full Version' not in [l for l in legend_labels]:
                    legend_handles.append(proxy_full_bar)
                    legend_labels.append('Vanilla Full Version')
                
                plt.bar_label(proxy_full_bar, labels=[f'{data[system]["proxy"]["full"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
            
            # Plot proxy lite on top of proxy full
            proxy_lite_val = data[system]['proxy'].get('lite', 0) / full_value
            if proxy_lite_val > 0:
                proxy_lite_bar = plt.bar(position, proxy_lite_val, bar_width,
                                       bottom=proxy_full_val,
                                       color='tab:blue', alpha=0.5)
                # Add to legend only once
                if 'Proxy Option' not in [l for l in legend_labels]:
                    legend_handles.append(proxy_lite_bar)
                    legend_labels.append('Proxy Option')
                
                plt.bar_label(proxy_lite_bar, labels=[f'{data[system]["proxy"]["lite"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
        
        elif bar_type == 'ebpf':
            # Plot ebpf full
            ebpf_full_val = data[system]['ebpf'].get('full', 0) / full_value
            if ebpf_full_val > 0:
                ebpf_full_bar = plt.bar(position, ebpf_full_val, bar_width,
                                      color='tab:green', alpha=0.5, hatch='++')
                # Add to legend only once
                if 'Full Version w/ LiteLib' not in [l for l in legend_labels]:
                    legend_handles.append(ebpf_full_bar)
                    legend_labels.append('Full Version w/ LiteLib')
                
                plt.bar_label(ebpf_full_bar, labels=[f'{data[system]["ebpf"]["full"]:.1f}'],
                            padding=0, fontsize=8, label_type='center', rotation=90,
                            bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
            
            # Plot ebpf lite on top of ebpf full
            ebpf_lite_val = data[system]['ebpf'].get('lite', 0) / full_value
            if ebpf_lite_val > 0:
                ebpf_lite_bar = plt.bar(position, ebpf_lite_val, bar_width,
                                      bottom=ebpf_full_val,
                                      color='tab:blue', alpha=0.5, hatch='\\\\')
                # Add to legend only once
                if 'eBPF Option' not in [l for l in legend_labels]:
                    legend_handles.append(ebpf_lite_bar)
                    legend_labels.append('eBPF Option')
                
                plt.bar_label(ebpf_lite_bar, labels=[f'{data[system]["ebpf"]["lite"]:.1f}'],
                            padding=0, fontsize=8, label_type='center', rotation=90,
                            bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
        
        elif bar_type == 'checkpoint':
            checkpoint_val = data[system]['checkpoint'] / full_value
            if checkpoint_val > 0:
                checkpoint_bar = plt.bar(position, checkpoint_val, bar_width,
                                       color='tab:orange', alpha=0.5, hatch='--')
                # Add to legend only once
                if 'Checkpoint' not in [l for l in legend_labels]:
                    legend_handles.append(checkpoint_bar)
                    legend_labels.append('Checkpoint')
                
                plt.bar_label(checkpoint_bar, labels=[f'{data[system]["checkpoint"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
        
        elif bar_type == 'replica':
            # Plot replica master
            replica_master_val = data[system]['replica'].get('master', 0) / full_value
            replica_other_val = data[system]['replica'].get('other', 0) / full_value
            
            if replica_master_val > 0:
                replica_master_bar = plt.bar(position, replica_master_val, bar_width,
                                          color='tab:purple', alpha=0.5, hatch='+++')
                # Add to legend only once
                if 'Active-passive' not in [l for l in legend_labels]:
                    legend_handles.append(replica_master_bar)
                    legend_labels.append('Active-passive')
                
                plt.bar_label(replica_master_bar, labels=[f'{data[system]["replica"]["master"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
            
            # Plot replica other on top of replica master
            if replica_other_val > 0:
                replica_other_bar = plt.bar(position, replica_other_val, bar_width,
                                         bottom=replica_master_val,
                                         color='tab:purple', alpha=0.5, hatch='x')
                
                plt.bar_label(replica_other_bar, labels=[f'{data[system]["replica"]["other"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))
        
        elif bar_type == 'embedded':
            embedded_val = data[system]['embedded'] / full_value
            if embedded_val > 0:
                embedded_bar = plt.bar(position, embedded_val, bar_width,
                                     color='tab:green', alpha=0.5, hatch='++')
                # Add to legend only once
                # if 'Embedded' not in [l for l in legend_labels]:
                #     legend_handles.append(embedded_bar)
                #     legend_labels.append('Embedded')
                
                plt.bar_label(embedded_bar, labels=[f'{data[system]["embedded"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))

        elif bar_type in ('ndb', 'ndb(client)', 'ndb(proxy)'):
            ndb_key = bar_type
            if ndb_key not in data[system]:
                continue
            ndb_data = data[system][ndb_key]
            ndb_node1_val = ndb_data.get('node1', 0) / full_value
            ndb_node2_val = ndb_data.get('node2', 0) / full_value
            ndb_other_val = ndb_data.get('other', 0) / full_value
            legend_label = 'Active-active (client)' if ndb_key == 'ndb(client)' else 'Active-active (proxy)' if ndb_key == 'ndb(proxy)' else 'Active-active'
            ndb_alpha = 0.45 if ndb_key == 'ndb(client)' else 0.75

            if ndb_node1_val > 0:
                ndb_node1_bar = plt.bar(position, ndb_node1_val, bar_width,
                                      color='tab:red', alpha=ndb_alpha, hatch='oo')
                if legend_label not in legend_labels:
                    legend_handles.append(ndb_node1_bar)
                    legend_labels.append(legend_label)
                plt.bar_label(ndb_node1_bar, labels=[f'{ndb_data["node1"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))

            if ndb_node2_val > 0:
                ndb_node2_bar = plt.bar(position, ndb_node2_val, bar_width,
                                      bottom=ndb_node1_val,
                                      color='tab:red', alpha=ndb_alpha, hatch='..')
                plt.bar_label(ndb_node2_bar, labels=[f'{ndb_data["node2"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))

            if ndb_other_val > 0:
                ndb_other_bar = plt.bar(position, ndb_other_val, bar_width,
                                      bottom=ndb_node1_val + ndb_node2_val,
                                      color='tab:red', alpha=ndb_alpha, hatch='xx')
                plt.bar_label(ndb_other_bar, labels=[f'{ndb_data["other"]:.1f}'],
                             padding=0, fontsize=8, label_type='center', rotation=90,
                             bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))

# Customize the plot
plt.ylabel('Normalized CPU Util')
tick_positions = [x[i] + (num_bars_per_system[i] - 2) * bar_interval / 2 for i in range(len(systems))]
plt.xticks(tick_positions, systems, rotation=0, fontsize=11)
plt.grid(True, axis='y')

# Create a separate figure for the legend
plt.figure(figsize=(1, 1))

# Define custom legend order to match the reference image
legend_order = [
    'Vanilla Full Version',
    'Checkpoint',
    'Full Version w/ LiteLib',
    'eBPF Option',
    'Proxy Option',
    'Active-passive',
    'Active-active (client)',
    'Active-active (proxy)',
]

# Filter out any legend items that don't exist in the current plot
ordered_handles = [legend_handles[legend_labels.index(label)] for label in legend_order if label in legend_labels]
ordered_labels = [label for label in legend_order if label in legend_labels]

# Close the dummy figure
plt.close()

# Switch back to the main figure
plt.figure(1)

# Adjust the legend position to prevent overlap
plt.legend(ordered_handles, ordered_labels,
          loc='lower left', bbox_to_anchor=(-0.2, 1.),
          ncol=4, frameon=False, fontsize=11)

# Save the plot
plt.savefig(args.output, bbox_inches='tight', pad_inches=0.1)
