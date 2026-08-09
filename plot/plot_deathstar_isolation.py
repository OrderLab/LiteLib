import json
import matplotlib.pyplot as plt
import numpy as np
import matplotlib
import argparse
import matplotlib.gridspec as gridspec

def plot_bar_label(bar, label):
    # Convert labels to list if they're not already
    label_list = [str(l) for l in label]
    # Replace '0.00' with empty string
    label_list = ['' if l == '0.00' else l for l in label_list]
    plt.bar_label(bar,
                 labels=label_list,
                 padding=0, fontsize=8, label_type='center',
                 bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2'),
                 rotation=90)

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

parser = argparse.ArgumentParser()
parser.add_argument("filenames", nargs="+", help="The path to the stats file(s)")
parser.add_argument("-o", "--output", help="The path to the output file")
args = parser.parse_args()

# Read the stats file
with open(args.filenames[0], 'r') as f:
    data = json.load(f)

# Group data by vanilla and litesys
vanilla_crash = [d for d in data['crash'] if d['log_file'].startswith('vanilla')]
litesys_crash = [d for d in data['crash'] if d['log_file'].startswith('litesys')]
vanilla_nocrash = [d for d in data['nocrash'] if d['log_file'].startswith('vanilla')]
litesys_nocrash = [d for d in data['nocrash'] if d['log_file'].startswith('litesys')]

# Calculate averages
def calculate_averages(crash_data, nocrash_data):
    before_crash = {
        'requests_sum_diff': np.mean([d['before_crash']['requests_sum_diff'] for d in nocrash_data]),
        'failover_all': np.mean([d['before_crash']['failover_all'] for d in nocrash_data]),
        'duration_avg': np.mean([d['before_crash']['duration_avg'] for d in nocrash_data])
    }
    after_crash = {
        'requests_sum_diff': np.mean([d['after_crash']['requests_sum_diff'] for d in crash_data]),
        'failover_all': np.mean([d['after_crash']['failover_all'] for d in crash_data]),
        'duration_avg': np.mean([d['after_crash']['duration_avg'] for d in crash_data])
    }
    return before_crash, after_crash

vanilla_before, vanilla_after = calculate_averages(vanilla_crash, vanilla_nocrash)
litesys_before, litesys_after = calculate_averages(litesys_crash, litesys_nocrash)

# Store original values
vanilla_requests_orig = [vanilla_before['requests_sum_diff'], vanilla_after['requests_sum_diff']]
vanilla_failover_orig = [vanilla_before['failover_all'], vanilla_after['failover_all']]
vanilla_regular_orig = [vanilla_requests_orig[i] - vanilla_failover_orig[i] for i in range(2)]

litesys_requests_orig = [litesys_before['requests_sum_diff'], litesys_after['requests_sum_diff']]
litesys_failover_orig = [litesys_before['failover_all'], litesys_after['failover_all']]
litesys_regular_orig = [litesys_requests_orig[i] - litesys_failover_orig[i] for i in range(2)]

vanilla_latency_orig = [vanilla_before['duration_avg'], vanilla_after['duration_avg']]
litesys_latency_orig = [litesys_before['duration_avg'], litesys_after['duration_avg']]

# Normalize all values to vanilla before crash
def normalize_values(value, baseline):
    return value / baseline

# Normalize request rates
vanilla_requests = [
    normalize_values(vanilla_before['requests_sum_diff'], vanilla_before['requests_sum_diff']),
    normalize_values(vanilla_after['requests_sum_diff'], vanilla_before['requests_sum_diff'])
]
vanilla_failover = [
    normalize_values(vanilla_before['failover_all'], vanilla_before['requests_sum_diff']),
    normalize_values(vanilla_after['failover_all'], vanilla_before['requests_sum_diff'])
]
vanilla_regular = [vanilla_requests[i] - vanilla_failover[i] for i in range(2)]

litesys_requests = [
    normalize_values(litesys_before['requests_sum_diff'], vanilla_before['requests_sum_diff']),
    normalize_values(litesys_after['requests_sum_diff'], vanilla_before['requests_sum_diff'])
]
litesys_failover = [
    normalize_values(litesys_before['failover_all'], vanilla_before['requests_sum_diff']),
    normalize_values(litesys_after['failover_all'], vanilla_before['requests_sum_diff'])
]
litesys_regular = [litesys_requests[i] - litesys_failover[i] for i in range(2)]

# Normalize latencies
vanilla_latency = [
    normalize_values(vanilla_before['duration_avg'], vanilla_before['duration_avg']),
    normalize_values(vanilla_after['duration_avg'], vanilla_before['duration_avg'])
]
litesys_latency = [
    normalize_values(litesys_before['duration_avg'], vanilla_before['duration_avg']),
    normalize_values(litesys_after['duration_avg'], vanilla_before['duration_avg'])
]

# Create figure with subplots
fig = plt.figure(figsize=(12, 3))
gs = gridspec.GridSpec(1, 3, width_ratios=[1, 1, 0.2], wspace=0.3)

# First subplot: Requests per second
ax1 = plt.subplot(gs[0])
x = np.array([0, 0.4])  # Reduced spacing between groups (was [0, 0.5])
bar_interval = 0.15
width = bar_interval - 0.05

# Plot vanilla bars
vanilla_regular_bars = ax1.bar(x - bar_interval/2, vanilla_regular, width, label='Vanilla Unredirected', color='tab:green', alpha=0.5, hatch='//')
vanilla_failover_bars = ax1.bar(x - bar_interval/2, vanilla_failover, width, bottom=vanilla_regular, label='Vanilla Redirected', color='tab:purple', alpha=0.8, hatch='\\\\')

# Plot litesys bars
litesys_regular_bars = ax1.bar(x + bar_interval/2, litesys_regular, width, label='LiteLib Unredirected', color='tab:blue', alpha=0.5, hatch='||')
litesys_failover_bars = ax1.bar(x + bar_interval/2, litesys_failover, width, bottom=litesys_regular, color='tab:purple', alpha=0.8, hatch='--')

# Add value labels with original values
plot_bar_label(vanilla_regular_bars, [f'{v:.2f}' for v in vanilla_regular_orig])
plot_bar_label(vanilla_failover_bars, [f'{v:.2f}' for v in vanilla_failover_orig])
plot_bar_label(litesys_regular_bars, [f'{v:.2f}' for v in litesys_regular_orig])
plot_bar_label(litesys_failover_bars, [f'{v:.2f}' for v in litesys_failover_orig])

ax1.set_ylabel('Normalized Requests Per Second')
ax1.set_xticks(x)
ax1.set_xticklabels(['Before Failure', 'After Failure'])
ax1.legend(loc='upper left', bbox_to_anchor=(-0.1, 1.2), ncol=2, frameon=False)
ax1.grid(True, axis='y', alpha=0.3)

# Add title at the bottom
ax1.text(0.5, -0.2, '(a) Requests Per Second',
         horizontalalignment='center', transform=ax1.transAxes)

# Second subplot: Average Latency with broken axis
gs2 = gridspec.GridSpecFromSubplotSpec(2, 1, subplot_spec=gs[1], height_ratios=[1, 3], hspace=0.05)
ax2_upper = fig.add_subplot(gs2[0])
ax2_lower = fig.add_subplot(gs2[1])

# Function to split values for broken axis
def split_value(value):
    if value <= 3:
        return value, 0
    else:
        return 3, value

# Split values for both systems
vanilla_lower = [split_value(v)[0] for v in vanilla_latency]
vanilla_upper = [split_value(v)[1] for v in vanilla_latency]
litesys_lower = [split_value(v)[0] for v in litesys_latency]
litesys_upper = [split_value(v)[1] for v in litesys_latency]

# Plot bars in lower axis
vanilla_latency_bars_lower = ax2_lower.bar(x - bar_interval/2, vanilla_lower, width, label='Vanilla', color='tab:green', alpha=0.5, hatch='//')
litesys_latency_bars_lower = ax2_lower.bar(x + bar_interval/2, litesys_lower, width, label='LiteLib', color='tab:blue', alpha=0.5, hatch='||')

# Plot bars in upper axis
vanilla_latency_bars_upper = ax2_upper.bar(x - bar_interval/2, vanilla_upper, width, label='Vanilla', color='tab:green', alpha=0.5, hatch='//')
litesys_latency_bars_upper = ax2_upper.bar(x + bar_interval/2, litesys_upper, width, label='LiteLib', color='tab:blue', alpha=0.5, hatch='||')

# Add value labels with original values for latency
plot_bar_label(vanilla_latency_bars_lower, [f'{vanilla_latency_orig[i]:.2f}' for i in range(len(vanilla_latency))])
plot_bar_label(litesys_latency_bars_lower, [f'{litesys_latency_orig[i]:.2f}' for i in range(len(litesys_latency))])
ax2_upper.bar_label(vanilla_latency_bars_upper, labels=[f'{v:.0f}x' if v > 0 else '' for v in vanilla_latency], padding=3)
ax2_upper.bar_label(litesys_latency_bars_upper, labels=[f'{v:.0f}x' if v > 0 else '' for v in litesys_latency], padding=3)

# Configure axes
ax2_lower.set_ylim(0, 3)
ax2_upper.set_ylim(8, max(max(vanilla_latency), max(litesys_latency)) * 1.5)

# Hide the appropriate spines
ax2_upper.spines['bottom'].set_visible(False)
ax2_lower.spines['top'].set_visible(False)
ax2_upper.xaxis.tick_top()
ax2_upper.xaxis.set_tick_params(labeltop=False)
ax2_upper.set_xticks([])  # Remove x-axis ticks from upper graph

# Add break lines
d = .015
kwargs = dict(transform=ax2_upper.transAxes, color='k', clip_on=False)
ax2_upper.plot((-d, +d), (-d, +d), **kwargs)
ax2_upper.plot((1 - d, 1 + d), (-d, +d), **kwargs)
kwargs.update(transform=ax2_lower.transAxes)
ax2_lower.plot((-d, +d), (1 - d, 1 + d), **kwargs)
ax2_lower.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)

# Set labels and grid
ax2_lower.set_xticks(x)
ax2_lower.set_xticklabels(['Before Failure', 'After Failure'])
ax2_lower.grid(True, axis='y', alpha=0.3)
ax2_upper.grid(True, axis='y', alpha=0.3)

# Add y label to the middle of both parts
ax2_lower.set_ylabel('Normalized Latency', y=0.65)

# Add legend with proper styling to upper axis
handles, labels = ax2_lower.get_legend_handles_labels()
ax2_upper.legend(handles, labels, loc='upper left', bbox_to_anchor=(0., 1.5), ncol=2, frameon=False)

# Add title at the bottom
ax2_lower.text(0.5, -0.27, '(b) Average Latency', 
               horizontalalignment='center', transform=ax2_lower.transAxes)

# Add baseline at y=1
ax1.axhline(y=1, color='black', linestyle='--', alpha=0.7)
ax2_lower.axhline(y=1, color='black', linestyle='--', alpha=0.7)

plt.savefig(args.output if args.output else 'deathstar_isolation.pdf', bbox_inches='tight', pad_inches=0.1)
