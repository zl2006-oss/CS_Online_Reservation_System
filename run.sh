#!/usr/bin/bash

# 强制杀掉所有相关进程（SIGKILL 信号）
pkill -9 -f "./server" 2>/dev/null
sleep 1                  # 等待端口完全释放

make clean
make

if [ -f "server" ]; then
    ulimit -n 1000000
    ./server
else
    echo "没有可执行程序"
fi