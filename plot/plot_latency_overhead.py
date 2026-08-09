import json
import matplotlib.pyplot as plt
import argparse
import matplotlib.pyplot as plt
import matplotlib
import numpy as np
import matplotlib.gridspec as gridspec

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

parser = argparse.ArgumentParser()
parser.add_argument("filenames", nargs="+", help="The path to the latency overhead file(s)")
parser.add_argument("-o", "--output", help="The path to the output file")
args = parser.parse_args()

def plot_bar_label(bar, label):
    plt.bar_label(bar,
                 labels=label,
                 padding=0, fontsize=8, label_type='center', rotation=90,
                 bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'))

with open(args.filenames[0], 'r') as f:
    data = json.load(f)

# Prepare data for plotting
systems = list(data.keys())

# Extract values for each system and metric, normalized to 'full'
values = []
original_values = {}  # Store original values for labels
for system in systems:
    full_value = data[system]['full']
    system_values = []
    original_values[system] = {}
    for metric in ['full', 'ebpf', 'embedded', 'proxy', 'checkpoint', 'replica', 'ndb(client)', 'ndb(proxy)']:
        if metric in data[system]:
            system_values.append(data[system][metric] / full_value)
            original_values[system][metric] = data[system][metric]  # Store original value
        else:
            system_values.append(0)
    values.append(system_values)

# Convert values to microseconds for plotting (not for labels)
for system in systems:
    for metric in data[system]:
        data[system][metric] /= 1000000

# Create the plot
fig = plt.figure(figsize=(8, 2))
gs = gridspec.GridSpec(2, 1, height_ratios=[1, 3], hspace=0.05)
ax1 = plt.subplot(gs[0])
ax2 = plt.subplot(gs[1])

# Set up broken axis appearance
ax1.spines['bottom'].set_visible(False)
ax2.spines['top'].set_visible(False)
ax1.tick_params(axis='x', which='both', bottom=False)

# Define break points
break_low = 1.7
break_high = 2.1
ax1.set_ylim(break_high, 4.5)
ax2.set_ylim(0, break_low)

# Set up bar positions so gap between rightmost bar of left group and leftmost bar of right group is constant
bar_interval = 0.25
bar_width = bar_interval - 0.05
gap_between_groups = 0.5
num_metrics_per_system = []
for system in systems:
    metrics = list(data[system].keys())
    positions = {}
    pos_idx = 0
    if 'full' in metrics:
        positions['full'] = pos_idx
        pos_idx += 1
    for variant in ['ebpf', 'embedded', 'proxy']:
        if variant in metrics:
            positions[variant] = pos_idx
            pos_idx += 1
    for metric in ['checkpoint', 'replica', 'ndb(client)', 'ndb(proxy)']:
        if metric in metrics:
            positions[metric] = pos_idx
            pos_idx += 1
    num_metrics_per_system.append(len(positions))
x = np.zeros(len(systems))
for j in range(len(systems) - 1):
    n_left = num_metrics_per_system[j]
    n_right = num_metrics_per_system[j + 1]
    x[j + 1] = x[j] + gap_between_groups + bar_interval * (n_left + n_right - 2) / 2

# Define bar styles and order based on the reference image
bar_styles = {
    'full': {'color': 'tab:green', 'hatch': '..', 'label': 'Baseline', 'order': 0},
    'ebpf': {'color': 'tab:blue', 'hatch': '\\\\', 'label': 'eBPF Option', 'order': 1},
    'embedded': {'color': 'tab:blue', 'hatch': '////', 'label': 'Embedded Option', 'order': 1},
    'proxy': {'color': 'tab:blue', 'hatch': '++', 'label': 'Proxy Option', 'order': 1},
    'checkpoint': {'color': 'tab:orange', 'hatch': '--', 'label': 'Checkpoint', 'order': 2},
    'replica': {'color': 'tab:purple', 'hatch': 'xx', 'label': 'Active-passive', 'order': 2},
    'ndb(client)': {'color': 'tab:red', 'hatch': 'oo', 'label': 'Active-active (client)', 'order': 2},
    'ndb(proxy)': {'color': 'tab:red', 'hatch': 'OO', 'label': 'Active-active (proxy)', 'order': 2}
}

# Plot bars in both axes
for ax in [ax1, ax2]:
    added_legends = set()
    
    # Get the metrics present in the data for each system and their positions
    system_positions = {}
    for system in systems:
        metrics = [k for k in data[system].keys()]
        # Sort metrics to ensure consistent order
        positions = {}
        pos_idx = 0
        # First position: full
        if 'full' in metrics:
            positions['full'] = pos_idx
            pos_idx += 1
        # Second position: variants (ebpf, embedded, proxy)
        for variant in ['ebpf', 'embedded', 'proxy']:
            if variant in metrics:
                positions[variant] = pos_idx
                pos_idx += 1
        # Third position: checkpoint/replica
        for metric in ['checkpoint', 'replica', 'ndb(client)', 'ndb(proxy)']:
            if metric in metrics:
                positions[metric] = pos_idx
                pos_idx += 1
        system_positions[system] = positions
    
    # Plot each type of bar
    for metric, style in bar_styles.items():
        metric_values = []
        metric_labels = []
        bar_positions = []
        
        for j, system in enumerate(systems):
            if metric in data[system]:
                metric_values.append(values[j][list(bar_styles.keys()).index(metric)])
                metric_labels.append(f'{original_values[system][metric]:.2f}')  # Use original value for label
                
                # Calculate position based on the metric's position in this system
                num_metrics = len(system_positions[system])
                metric_pos = system_positions[system][metric]
                position = x[j] + (metric_pos - (num_metrics - 1)/2) * bar_interval
                bar_positions.append(position)
            else:
                metric_values.append(0)
                metric_labels.append('')
                bar_positions.append(x[j])
        
        label = style['label'] if style['label'] not in added_legends else None
        bars = ax.bar(bar_positions,
                     [min(v, break_low) if ax == ax2 else (3.5 if v > 3.8 else v) for v in metric_values],
                     bar_width,
                     label=label,
                     color=style['color'],
                     alpha=0.5,
                     hatch=style['hatch'])
        
        if label:
            added_legends.add(style['label'])
        
        if ax == ax2:
            plot_bar_label(bars, metric_labels)
        elif metric in ('checkpoint', 'ndb(client)', 'ndb(proxy)'):
            plt.bar_label(bars,
                        labels=[f'{v:.2f}x' for v in metric_values],
                        fontsize=8, padding=12, label_type='center')

# Add break marks
d = .015
kwargs = dict(transform=ax1.transAxes, color='k', clip_on=False)
ax1.plot((-d, +d), (-d, +d), **kwargs)
ax1.plot((1 - d, 1 + d), (-d, +d), **kwargs)
kwargs.update(transform=ax2.transAxes)
ax2.plot((-d, +d), (1 - d, 1 + d), **kwargs)
ax2.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)
ax1.set_yticks([])

# Customize the plot
fig.text(0.04, 0.5, 'Normalized Latency', va='center', rotation='vertical', fontsize=11)
ax2.set_xticks(x)
ax2.set_xticklabels(systems, rotation=0, fontsize=11)

# Move legend to top of figure
handles, labels = ax1.get_legend_handles_labels()
# Define custom legend order to match the reference image
legend_order = [
    'Baseline',
    'Checkpoint',
    'eBPF Option',
    'Proxy Option',
    'Embedded Option',
    'Active-passive',
    'Active-active (client)',
    'Active-active (proxy)',
]
# Filter out any legend items that don't exist in the current plot
ordered_handles = [handles[labels.index(label)] for label in legend_order if label in labels]
ordered_labels = [label for label in legend_order if label in labels]
ax1.legend(ordered_handles, ordered_labels, loc='lower left', bbox_to_anchor=(-0.15, 1.05), 
          ncol=4, frameon=False, fontsize=11)

# Add grid to both axes
ax1.grid(True, axis='y')
ax2.grid(True, axis='y')

# Add baseline at y=1 to both axes
ax1.axhline(y=1, color='black', linestyle='--', alpha=0.7)
ax2.axhline(y=1, color='black', linestyle='--', alpha=0.7)

# Save the plot
plt.savefig(args.output, bbox_inches='tight', pad_inches=0.1)
