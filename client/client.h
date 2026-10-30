#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <vector>
#include <jsoncpp/json/json.h>
using namespace std;

// OPTYPE: 客户端与服务器通信的操作类型枚举
// 与 server.h 中的定义保持一致，用于 JSON 报文中的 type 字段
enum OPTYPE
{
    DL = 1,   // 登录（Login）
    ZC,       // 注册（Register）
    CKYY,     // 查看可预约信息（ChaKan YuYue）
    YD,       // 预定车票（YuDing）
    WDYY,     // 查看我的预约（WoDe YuYue）
    QXYY,     // 取消预约（QuXiao YuYue）
    TC        // 退出客户端（TuiChu）
};

// TcpClient: TCP 客户端类
// 负责与服务器建立连接、发送 JSON 请求、接收并解析 JSON 响应、驱动菜单交互
class TcpClient
{
public:
    // 带参构造函数
    // 参数 ser_ips: 服务器 IP 地址
    // 参数 ser_port: 服务器端口号
    TcpClient(string ser_ips, short ser_port) : ips(ser_ips), port(ser_port)
    {
        running = true;
        login_status = false;
        op_type = -1;
    }

    // 默认构造函数，连接到本机 127.0.0.1:6000
    TcpClient()
    {
        ips = "127.0.0.1";
        port = 6000;
        running = true;
        login_status = false;
        op_type = -1;
    }

    // 析构函数：关闭已建立的套接字，释放系统资源
    ~TcpClient()
    {
        close(sockfd);
    }

    // Socket_Init: 创建 TCP 套接字并连接服务器
    // 返回值: true 表示连接成功；false 表示创建套接字或连接失败
    bool Socket_Init();

    // Run: 客户端主循环，循环打印菜单并根据用户选择分发到对应业务函数
    void Run();

private:
    // Print_info: 打印用户菜单并读取用户输入的操作序号，结果存入 op_type
    // 根据 login_status 区分"未登录菜单"与"已登录菜单"
    void Print_info();

    // sendJson: 将 Json 对象序列化为字符串，末尾追加 \n 后发送给服务器
    // 参数 val: 待发送的 Json 对象
    // 返回值: true 发送成功；false 发送失败
    bool sendJson(const Json::Value &val);

    // recvJson: 循环 recv 直到收到以 \n 结尾的完整消息，解析为 Json 对象
    // 参数 res(出参): 解析后的 Json 对象
    // 返回值: true 接收并解析成功；false 连接关闭或解析失败
    bool recvJson(Json::Value &res);

    // User_Register: 用户注册
    // 读取手机号/用户名/密码，封装 JSON 发送给服务器，注册成功后自动置登录状态
    void User_Register();

    // User_Login: 用户登录
    // 读取手机号/密码，发送登录请求，成功后保存用户名并置登录状态
    void User_Login();

    // Show_Ticket: 查看所有可预约的车票信息列表
    // 发送 CKYY 请求，解析服务器返回的 ticket_arr 数组并按表格打印
    void Show_Ticket();

    // Yd_Ticket: 预定车票
    // 先调用 Show_Ticket 展示列表，让用户输入序号，再发送 YD 请求
    void Yd_Ticket();

    // Show_My_Ticket: 查看当前用户的预约记录
    // 发送 WDYY 请求（携带 user_tel），解析 my_ticket_arr 数组并打印
    void Show_My_Ticket();

    // Cancel_Ticket: 取消预约
    // 先调用 Show_My_Ticket 展示记录，让用户输入序号，再发送 QXYY 请求
    void Cancel_Ticket();

private:
    string ips;          // 服务器 IP 地址
    short port;          // 服务器端口号
    int sockfd;          // 客户端套接字文件描述符
    bool running;        // 客户端主循环运行标志，true 运行中，false 退出
    bool login_status;   // 登录状态，true 已登录，false 未登录
    int op_type;         // 当前操作类型（OPTYPE 枚举值或 -1 表示无效）
    string username;     // 当前登录用户的用户名（登录成功后由服务器返回）
    string usertel;      // 当前用户的手机号（注册或登录时输入）
    vector<int> my_tk_ids; // 我的预约列表对应的 tk_id，下标与显示序号对应
    vector<int> tk_ids;    // 可预约票务列表对应的 tk_id，下标与显示序号对应
};

#endif // CLIENT_H
