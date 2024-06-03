#!/bin/bash

# 日志文件
LOG_FILE="benchmark.log"
# 输出 CSV 文件
OUTPUT_CSV="benchmark_rps.csv"

# 初始化 CSV 文件，添加表头
echo "timestamp,operation,rps" > $OUTPUT_CSV

# 函数：读取日志文件并解析 RPS 值，附上时间戳
process_log() {
    tail -n 0 -F $LOG_FILE | while read -r line; do
        echo $line
        if [[ $line =~ ^([A-Z]+):\ rps=([0-9\.]+) ]]; then
            operation="${BASH_REMATCH[1]}"
            rps="${BASH_REMATCH[2]}"
            timestamp=$(date +%s)
            echo "$timestamp,$operation,$rps" >> $OUTPUT_CSV
        fi
    done
}

# 主循环：处理日志文件，处理文件截断情况
while true; do
    process_log
    echo "Log file truncated, restarting..."
    sleep 1
done
