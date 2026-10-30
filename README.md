# CS_Online_Reservation_System

票务预约系统 —— C/S（Client/Server）架构练手项目。

服务端基于 **epoll + 线程池 + MySQL 连接池**，客户端通过 **JSON** 报文与服务端交互，另附一个多进程高并发压测工具。

## 功能

| 操作 | 枚举 | 说明 |
| --- | --- | --- |
| 登录 | `DL` | 用户登录 |
| 注册 | `ZC` | 用户注册 |
| 查看可预约票务 | `CKYY` | 查询当前可预约的票务信息 |
| 预定票务 | `YD` | 提交预约 |
| 查看我的预约 | `WDYY` | 查询本人已预约记录 |
| 取消预约 | `QXYY` | 取消已预约记录 |
| 退出 | `TC` | 断开客户端 |

## 项目结构

```
CS_Online_Reservation_System
├── server.h                 服务端头文件（各模块声明）
├── server.cpp               服务端实现
├── stress_client.cpp        多进程并发压测工具
├── client/
│   ├── client.h             客户端类与操作枚举
│   └── client.cpp           客户端实现（菜单交互）
├── my.conf                  服务端 / 数据库配置
├── makefile
├── run.sh                   一键编译并启动服务端
└── .vscode/                 VS Code 调试与 IntelliSense 配置
```

## 服务端模块划分

| 模块 | 职责 |
| --- | --- |
| `SerConf` | 解析 `my.conf`，保存服务器 / 数据库配置（缺失时用默认值兜底） |
| `ThreadPool` | 基于环形队列的线程池，异步处理客户端请求 |
| `MysqlClient` | 封装 MySQL 连接与全部业务 SQL（注册 / 登录 / 票务 / 预约） |
| `ConnPool` | MySQL 连接池（单例），预创建连接供多线程复用 |
| `ConnGuard` | RAII 守卫，自动从连接池取 / 还连接 |
| `Socket / LisSocket / ConSocket` | epoll 模型下的套接字抽象 |
| `TcpSer` | 服务器主控：socket + epoll + 线程池的组装与事件循环 |

## 配置（`my.conf`）

```ini
ip 127.0.0.1
port 6000
lismax 100000        # listen 队列长度，十万级并发需调大
threadnum 4          # 工作线程数
taskmax 1024         # 任务队列长度
db_ip 127.0.0.1
db_port 3306
db_name 2501project
db_username root
db_passwd ******     # 请填入自己的数据库密码
pool_size 8          # 连接池大小
```

> ⚠️ 仓库中的 `my.conf` 为本地开发配置，**含数据库密码**，提交到公开仓库前请自行替换为占位符。

## 编译与运行

依赖：`g++`、`libjsoncpp-dev`、`libmysqlclient-dev`

```bash
# 服务端（含压测工具）
make                 # 生成 server 和 stress
./run.sh             # make clean + make + 启动 server

# 客户端
cd client
g++ -o client client.cpp -ljsoncpp -lpthread
./client
```

## 高并发压测

`stress` 使用 `fork()` 拆分子进程，绕开单进程 fd 上限，验证服务器能否撑住十万级长连接保活。

```bash
ulimit -n 1000000            # 先放开本机 fd 上限
./stress 127.0.0.1 6000 100000 16   # <IP> <端口> <总连接数> [进程数]
```

## License

MIT
