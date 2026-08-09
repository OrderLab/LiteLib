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
parser.add_argument("filenames", nargs="+", help="The path to the CPU overhead file(s)")
parser.add_argument("-o", "--output", help="The path to the output file")
args = parser.parse_args()

with open(args.filenames[0], 'r') as f:
    data = json.load(f)

# Prepare data for plotting
systems = list(data.keys())
metrics = ['lite/lite', 'lite/full+', 'checkpoint']

def plot_bar_label(ax, bar, label, label_type='center', use_background=True):
    bbox_props = dict(facecolor='white', edgecolor='none', alpha=0.7, boxstyle='round,pad=0.2') if use_background else None
    ax.bar_label(bar,
                 labels=label,
                 padding=0, fontsize=8, label_type=label_type,
                 bbox=bbox_props)

def get_baseline_value(system_data, system):
    if 'full' in system_data:
        return system_data['full']
    if 'replica' in system_data and 'master' in system_data['replica']:
        return system_data['replica']['master']
    raise ValueError(f"No baseline value found for system {system}")

def format_gb(value):
    return f'{value/1024/1024/1024:.2f}'

# Extract values for each system and metric, normalized to 'full' or replica master
baseline_values = []
baseline_labels = []
lite_full_values = []
lite_full_labels = []
lite_lite_values = []
lite_lite_labels = []
checkpoint_values = []
checkpoint_labels = []
replica_master_values = []
replica_master_labels = []
replica_other_values = []
replica_other_labels = []
ndb_client_values = []
ndb_client_labels = []
ndb_proxy_values = []
ndb_proxy_labels = []

for system in systems:
    system_data = data[system]
    baseline_value = get_baseline_value(system_data, system)

    if 'full' in system_data:
        baseline_values.append(1)
        baseline_labels.append(format_gb(system_data['full']))
    else:
        baseline_values.append(0)
        baseline_labels.append('')

    if 'lite' in system_data:
        lite_data = system_data['lite']
        lite_full_value = lite_data.get('full+', lite_data.get('full', 0))
        lite_full_values.append(lite_full_value / baseline_value if lite_full_value else 0)
        lite_full_labels.append(format_gb(lite_full_value) if lite_full_value else '')
        lite_lite_value = lite_data.get('lite', 0)
        lite_lite_values.append(lite_lite_value / baseline_value if lite_lite_value else 0)
        lite_lite_labels.append(format_gb(lite_lite_value) if lite_lite_value else '')
    else:
        lite_full_values.append(0)
        lite_full_labels.append('')
        lite_lite_values.append(0)
        lite_lite_labels.append('')

    if 'checkpoint' in system_data:
        checkpoint_values.append(system_data['checkpoint'] / baseline_value)
        checkpoint_labels.append(format_gb(system_data['checkpoint']))
    else:
        checkpoint_values.append(0)
        checkpoint_labels.append('')

    if 'replica' in system_data:
        replica_master_values.append(system_data['replica']['master'] / baseline_value)
        replica_master_labels.append(format_gb(system_data['replica']['master']))
        replica_other_values.append(system_data['replica']['other'] / baseline_value)
        replica_other_labels.append(format_gb(system_data['replica']['other']))
    else:
        replica_master_values.append(0)
        replica_master_labels.append('')
        replica_other_values.append(0)
        replica_other_labels.append('')

    if 'ndb(client)' in system_data:
        ndb_total = (
            system_data['ndb(client)'].get('node1', 0)
            + system_data['ndb(client)'].get('node2', 0)
            + system_data['ndb(client)'].get('other', 0)
        )
        ndb_client_values.append(ndb_total / baseline_value if ndb_total else 0)
        ndb_client_labels.append(format_gb(ndb_total) if ndb_total else '')
    else:
        ndb_client_values.append(0)
        ndb_client_labels.append('')
    if 'ndb(proxy)' in system_data:
        ndb_total = (
            system_data['ndb(proxy)'].get('node1', 0)
            + system_data['ndb(proxy)'].get('node2', 0)
            + system_data['ndb(proxy)'].get('other', 0)
        )
        ndb_proxy_values.append(ndb_total / baseline_value if ndb_total else 0)
        ndb_proxy_labels.append(format_gb(ndb_total) if ndb_total else '')
    else:
        ndb_proxy_values.append(0)
        ndb_proxy_labels.append('')

# Create the plot
fig = plt.figure(figsize=(8, 2.2))
gs = gridspec.GridSpec(2, 1, height_ratios=[1, 3], hspace=0.05)
ax1 = plt.subplot(gs[0])
ax2 = plt.subplot(gs[1])

# Set up broken axis appearance
ax1.spines['bottom'].set_visible(False)
ax2.spines['top'].set_visible(False)
ax1.tick_params(axis='x', which='both', bottom=False)

# Define break points
break_low = 2
break_high = 3
max_group_values = [
    max(baseline_values, default=0),
    max([f + l for f, l in zip(lite_full_values, lite_lite_values)], default=0),
    max(checkpoint_values, default=0),
    max([m + o for m, o in zip(replica_master_values, replica_other_values)], default=0),
    max(ndb_client_values + ndb_proxy_values, default=0),
]
max_y_value = max(max_group_values + [break_high + 0.5])
max_y_value = max_y_value * 2.0 if max_y_value > break_high else break_high + 0.5
ax1.set_ylim(break_high, max_y_value)
ax2.set_ylim(0, break_low)

# Set up bar positions so gap between rightmost bar of left group and leftmost bar of right group is constant
bar_interval = 0.25
bar_width = bar_interval - 0.05
gap_between_groups = 0.5

group_order = ['baseline', 'lite', 'checkpoint', 'replica', 'ndb(client)', 'ndb(proxy)']
system_groups = {}
for sys in systems:
    groups = []
    if 'full' in data[sys]:
        groups.append('baseline')
    if 'lite' in data[sys]:
        groups.append('lite')
    if 'checkpoint' in data[sys]:
        groups.append('checkpoint')
    if 'replica' in data[sys]:
        groups.append('replica')
    if 'ndb(client)' in data[sys]:
        groups.append('ndb(client)')
    if 'ndb(proxy)' in data[sys]:
        groups.append('ndb(proxy)')
    system_groups[sys] = groups

num_groups_per_system = [len(system_groups[sys]) for sys in systems]
x = np.zeros(len(systems))
for j in range(len(systems) - 1):
    n_left = num_groups_per_system[j]
    n_right = num_groups_per_system[j + 1]
    x[j + 1] = x[j] + gap_between_groups + bar_interval * (n_left + n_right - 2) / 2

def group_positions(group):
    positions = []
    for i, sys in enumerate(systems):
        groups = system_groups[sys]
        if group in groups:
            idx = groups.index(group)
            positions.append(x[i] + (idx - (len(groups) - 1) / 2) * bar_interval)
        else:
            positions.append(x[i])
    return positions

baseline_positions = group_positions('baseline')
lite_positions = group_positions('lite')
checkpoint_positions = group_positions('checkpoint')
replica_positions = group_positions('replica')
ndb_client_positions = group_positions('ndb(client)')
ndb_proxy_positions = group_positions('ndb(proxy)')

def apply_labels(ax, bars, labels, values, label_type='center', use_background=True):
    if ax == ax2:
        target_labels = labels
    else:
        target_labels = ['' for _ in labels]
    if any(target_labels):
        plot_bar_label(ax, bars, target_labels, label_type=label_type, use_background=use_background)

for ax in [ax1, ax2]:
    if any(v > 0 for v in baseline_values):
        baseline_bars = ax.bar(baseline_positions, baseline_values, bar_width,
                label='Vanilla Full Version', color='tab:green', alpha=0.5, hatch='..')
        apply_labels(ax, baseline_bars, baseline_labels, baseline_values)

    if any(v > 0 for v in lite_full_values):
        full_bars = ax.bar(lite_positions, lite_full_values, bar_width,
                label='Full Version w/ LiteLib', color='tab:green', alpha=0.5,
                hatch=['++' if "lite" in data[sys] and "full+" in data[sys]["lite"] else '..' for sys in systems])
        apply_labels(ax, full_bars, lite_full_labels, lite_full_values)

    if any(v > 0 for v in lite_lite_values):
        lite_bars = ax.bar(lite_positions, lite_lite_values, bar_width,
                bottom=lite_full_values,
                label='Lite Replica', color='tab:blue', alpha=0.5)
        apply_labels(ax, lite_bars, lite_lite_labels, lite_lite_values, label_type='edge', use_background=False)

    if any(v > 0 for v in checkpoint_values):
        checkpoint_bars = ax.bar(checkpoint_positions, checkpoint_values, bar_width,
                label='Checkpoint', color='tab:orange', alpha=0.5, hatch='--')
        apply_labels(ax, checkpoint_bars, checkpoint_labels, checkpoint_values)

    if any(v > 0 for v in replica_master_values):
        master_bars = ax.bar(replica_positions, replica_master_values, bar_width,
                label='Active-passive (Master)', color='tab:purple', alpha=0.5, hatch='+++')
        apply_labels(ax, master_bars, replica_master_labels, replica_master_values)
        other_bars = ax.bar(replica_positions, replica_other_values, bar_width,
                bottom=replica_master_values,
                label='Active-passive (Other)', color='tab:purple', alpha=0.5, hatch='x')
        apply_labels(ax, other_bars, replica_other_labels, replica_other_values)

    if any(v > 0 for v in ndb_client_values):
        ndb_client_bars = ax.bar(ndb_client_positions, ndb_client_values, bar_width,
                label='Active-active (client)', color='tab:red', alpha=0.5, hatch='oo')
        apply_labels(ax, ndb_client_bars, ndb_client_labels, ndb_client_values)
        if ax == ax1:
            ax.bar_label(ndb_client_bars,
                         labels=[f'{v:.2f}x' if v > break_high else '' for v in ndb_client_values],
                         fontsize=8, padding=10, label_type='center')
    if any(v > 0 for v in ndb_proxy_values):
        ndb_proxy_bars = ax.bar(ndb_proxy_positions, ndb_proxy_values, bar_width,
                label='Active-active (proxy)', color='tab:red', alpha=0.5, hatch='OO')
        apply_labels(ax, ndb_proxy_bars, ndb_proxy_labels, ndb_proxy_values)
        if ax == ax1:
            ax.bar_label(ndb_proxy_bars,
                         labels=[f'{v:.2f}x' if v > break_high else '' for v in ndb_proxy_values],
                         fontsize=8, padding=18, label_type='center')

# Customize the plot
fig.text(0.04, 0.5, 'Normalized Memory Usage', va='center', rotation='vertical', fontsize=11)
ax2.set_xticks(x)
ax2.set_xticklabels(systems, rotation=0, fontsize=11)

# Add break marks
d = .015
kwargs = dict(transform=ax1.transAxes, color='k', clip_on=False)
ax1.plot((-d, +d), (-d, +d), **kwargs)
ax1.plot((1 - d, 1 + d), (-d, +d), **kwargs)
kwargs.update(transform=ax2.transAxes)
ax2.plot((-d, +d), (1 - d, 1 + d), **kwargs)
ax2.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)
ax1.set_yticks([])

# Move legend to top of figure
handles, labels = ax1.get_legend_handles_labels()
# Manual legend ordering (edit this list as needed).
legend_order = [
    'Vanilla Full Version',
    'Full Version w/ LiteLib',
    'Lite Replica',
    'Checkpoint',
    'Active-passive (Master)',
    'Active-passive (Other)',
    'Active-active (client)',
    'Active-active (proxy)',
]
ordered_handles = [handles[labels.index(label)] for label in legend_order if label in labels]
ordered_labels = [label for label in legend_order if label in labels]
ax1.legend(ordered_handles, ordered_labels, loc='lower left', bbox_to_anchor=(-0.2, 1.3),
          ncol=4, frameon=False, fontsize=11)

# Add grid to both axes
ax1.grid(True, axis='y')
ax2.grid(True, axis='y')

# Add a horizontal line at y=1 to show baseline
ax1.axhline(y=1, color='black', linestyle='--', alpha=0.7)
ax2.axhline(y=1, color='black', linestyle='--', alpha=0.7)

# Save the plot
plt.savefig(args.output, bbox_inches='tight', pad_inches=0.1)
