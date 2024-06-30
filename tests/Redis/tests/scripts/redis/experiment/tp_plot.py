import matplotlib.pyplot as plt
import re

# Read the data from the text file
data_file = 'throughput.txt'
data = []

with open(data_file, 'r') as file:
    for line in file:
        match = re.match(r'Time: (\d+)s - Success: (\d+), Missing: (\d+), Error: (\d+), Latency: ([\d.]+)ms', line)
        if match:
            time = int(match.group(1))
            success = int(match.group(2))
            missing = int(match.group(3))
            error = int(match.group(4))
            latency = float(match.group(5))
            data.append({"Time": time, "Success": success, "Missing": missing, "Error": error, "Latency": latency})

# Extract data for plotting
times = [entry["Time"] for entry in data]
successes = [entry["Success"] for entry in data]
missings = [entry["Missing"] for entry in data]
errors = [entry["Error"] for entry in data]
latencies = [entry["Latency"] for entry in data]

# Create the plot
fig, ax1 = plt.subplots()

# Plot Success, Missing, Error as filled areas
ax1.fill_between(times, successes, color='g', alpha=0.5, label='Success')
ax1.fill_between(times, [s + m for s, m in zip(successes, missings)], successes, color='r', alpha=0.5, label='Missing')
# ax1.fill_between(times, [s + m + e for s, m, e in zip(successes, missings, errors)], 
#                  [s + m for s, m in zip(successes, missings)], color='b', alpha=0.5, label='Error')

ax1.set_xlabel('Time (s)')
ax1.set_ylabel('Throughput')
ax1.legend(loc='upper left')

# Create a second y-axis for the latency
ax2 = ax1.twinx()
ax2.plot(times, latencies, 'k-', label='Latency')
ax2.set_ylabel('Latency (ms)')
ax2.legend(loc='upper right')

# Show the plot
plt.title('Throughput and Latency over Time')
plt.savefig('tp_result.png')
