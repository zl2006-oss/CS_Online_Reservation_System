/********************************************************************
 *  stress_client.cpp —— 高并发"连接保活"压测工具
 *
 * 用途：
 *   建立 N 个 TCP 长连接到服务器并持续保持（不发送业务数据），
 *   用于验证服务器能否撑住"十万连接保活"。
 *
 * 原理：
 *   单进程能够同时打开的文件描述符受 ulimit -n 限制，无法在
 *   一个进程里建到十万。因此用 fork() 拆成多个子进程，每个子
 *   进程负责建立并保持一部分连接，父进程通过管道汇总成功数。
 *
 * 用法：
 *   ./stress <服务器IP> <端口> <总连接数> [进程数，默认自动]
 *   例：  ./stress 127.0.0.1 6000 100000 16
 *
 * 前置：
 *   1) 先调大本机(客户端)文件描述符上限：  ulimit -n 1000000
 *   2) 若服务端在远程，把 IP 换成服务端地址，并放行端口
 ********************************************************************/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
using namespace std;

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "用法: %s <服务器IP> <端口> <总连接数> [进程数(默认自动)]\n", argv[0]);
        fprintf(stderr, "示例: %s 127.0.0.1 6000 100000 16\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    long total = atol(argv[3]);
    // 进程数：默认按每个进程约 5000 个连接自动拆分，避免单进程 fd 超限
    int procs = (argc >= 5) ? atoi(argv[4]) : 0;
    if (procs <= 0)
    {
        procs = (int)(total / 5000);
        if (procs < 1)
            procs = 1;
    }
    long perProc = total / procs;
    long left = total % procs;
    fprintf(stderr, "[压测] ip=%s port=%d total=%ld procs=%d perProc=%ld\n",
            ip, port, total, procs, perProc);

    long total_ok = 0;
    for (int p = 0; p < procs; p++)
    {
        long n = perProc + ((p < left) ? 1 : 0);

        int fds[2]; // 子进程 -> 父进程 汇总成功数的管道
        if (pipe(fds) != 0)
        {
            perror("pipe");
            return 1;
        }

        pid_t pid = fork();
        if (pid == 0) // 子进程：建立并保持本子进程负责的连接
        {
            close(fds[0]);
            vector<int> conns;
            conns.reserve(n);

            struct sockaddr_in saddr;
            memset(&saddr, 0, sizeof(saddr));
            saddr.sin_family = AF_INET;
            saddr.sin_port = htons(port);
            saddr.sin_addr.s_addr = inet_addr(ip);

            long ok = 0;
            for (long i = 0; i < n; i++)
            {
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                if (fd < 0)
                {
                    fprintf(stderr, "[%d] socket 失败，已成功 %ld，errno=%d\n", p, ok, errno);
                    break;
                }
                if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) == 0)
                {
                    conns.push_back(fd);
                    ok++;
                    if (ok % 10000 == 0)
                        fprintf(stderr, "[%d] 已建立 %ld\n", p, ok);
                }
                else
                {
                    fprintf(stderr, "[%d] connect 失败于 ok=%ld，errno=%d\n", p, ok, errno);
                    close(fd);
                    break;
                }
            }

            // 把成功数发给父进程，然后保持所有连接（不 close，一直挂起）
            long okW = ok;
            write(fds[1], &okW, sizeof(okW));
            close(fds[1]);
            fprintf(stderr, "[%d] 建立完成: %ld\n", p, ok);
            for (;;)
                sleep(3600); // 保持连接存活
        }
        else // 父进程：读取该子进程成功数并累计
        {
            close(fds[1]);
            long okR = 0;
            read(fds[0], &okR, sizeof(okR));
            close(fds[0]);
            fprintf(stderr, "[压测] 子进程%d 成功建连: %ld\n", p, okR);
            total_ok += okR;
        }
    }

    fprintf(stderr, "\n==== 压测统计：总共成功建立 %ld 条连接 ====\n", total_ok);
    fprintf(stderr, "连接保持中，请对照服务器端[压测]当前存活连接数确认是否到达 %ld\n", total);
    fprintf(stderr, "按 Ctrl+C 退出压测（会断开所有连接）。\n");

    // 父进程挂起，让各子进程继续保活
    for (;;)
        sleep(3600);
    return 0;
}