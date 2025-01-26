import pandas as pd

import matplotlib.pyplot as plt

# Read the CSV file into a pandas DataFrame
df = pd.read_csv('benchmark_results.csv')

# Extract the timestamp and ops_per_sec columns
timestamp = df['timestamp']
ops_per_sec = df['ops_per_sec']

# Plot the data
plt.plot(timestamp, ops_per_sec)
plt.xlabel('Timestamp')
plt.ylabel('Ops per sec')
plt.title('Benchmark Results')
plt.savefig('benchmark_results.png')