import re
import sys
import numpy as np

file_name = sys.argv[1]

results = {}

with open(file_name, 'r') as file:
  lines = file.readlines()

pattern = r'"([^"]+): (\d+\.\d+) us"'
for line in lines:
  match = re.search(pattern, line)
  if match:
    message, time = match.groups()
    time = float(time)

    if message in results:
      results[message].append(time)
    else:
      results[message] = [time]

for message, times in results.items():
  length = len(times)
  avg = np.mean(times)
  percentile_95 = np.percentile(times, 95)
  print(f'{message}: count = {length}, Average = {avg}, 95th percentile = {percentile_95}')