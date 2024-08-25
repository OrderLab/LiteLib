import subprocess
import time
import csv
import re
import datetime
import matplotlib.pyplot as plt

# CSV文件名
csv_filename = 'benchmark_results.csv'
# 图像文件名
image_filename = 'benchmark_results.png'

# 正则表达式匹配Ops per sec的数值
ops_per_sec_pattern = re.compile(r'Ops per sec:\s+([0-9.]+)')

# 初始化数据存储
timestamps = []
ops_per_sec_values = []

# 初始化CSV文件
with open(csv_filename, mode='w', newline='') as file:
    writer = csv.writer(file)
    writer.writerow(['timestamp', 'ops_per_sec'])

# 定义执行测试并记录结果的函数
def run_benchmark():
    result = subprocess.run(
        # ['hadoop', 'org.apache.hadoop.hdfs.server.namenode.NNThroughputBenchmark', 
        #  '-fs', 'hdfs://nna:9000', '-op', 'open', '-threads','3', '-files', '1000', 
        #  '-filesPerDir', '100000','-useExisting' ,'-keepResults'], 
        # ['hadoop', 'org.apache.hadoop.hdfs.server.namenode.NNThroughputBenchmark', 
        #  '-fs', 'hdfs://nna:9000', '-op', 'open', '-threads','10', '-files', '1000', 
        #  '-filesPerDir', '1000000'],
        ['hadoop', 'org.apache.hadoop.hdfs.server.namenode.NNThroughputBenchmark', 
         '-fs', 'hdfs://nna:9000', '-op', 'create', '-threads','3', '-files', '1000', 
         '-filesPerDir', '1000000'],
        capture_output=True, text=True
    )
    # print(result.stderr)
    timestamp = int(time.time())
    match = ops_per_sec_pattern.search(result.stderr)
    if match:
        ops_per_sec = float(match.group(1))
        with open(csv_filename, mode='a', newline='') as file:
            writer = csv.writer(file)
            writer.writerow([timestamp, ops_per_sec])
        timestamps.append(datetime.datetime.fromtimestamp(timestamp))
        ops_per_sec_values.append(ops_per_sec)
        print(f"{datetime.datetime.fromtimestamp(timestamp)} - Ops per sec: {ops_per_sec}")

# 初始化图形
fig, ax = plt.subplots()

# 定义更新图形的函数
def update_plot():
    ax.clear()
    ax.plot(timestamps, ops_per_sec_values, '-')
    ax.set_title('Ops per sec over time')
    ax.set_xlabel('Time')
    ax.set_ylabel('Ops per sec')
    fig.autofmt_xdate()

# subprocess.run(['hadoop', 'org.apache.hadoop.hdfs.server.namenode.NNThroughputBenchmark', 
#          '-fs', 'hdfs://nna:9000', '-op', 'clean'])
# 获取开始时间
start_time = time.time()

# 持续执行测试，直到60秒结束
while time.time() - start_time <90:
    run_benchmark()
    # update_plot()
    # plt.pause(0.1)  # 暂停以便图形更新

# 保存最终图像
update_plot()
plt.savefig(image_filename)
# plt.show()

# hadoop org.apache.hadoop.hdfs.server.namenode.NNThroughputBenchmark -fs hdfs://nna:9000 -op clean
# hadoop org.apache.hadoop.hdfs.server.namenode.NNThroughputBenchmark -fs hdfs://nna:9000 -op open -threads 1000 -files 100000 -filesPerDir 1000000 -keepResults