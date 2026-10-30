#include "client.h"

// Socket_Init: 创建 TCP 套接字并连接到服务器
// 返回值: true 表示连接成功；false 表示 socket 创建失败或 connect 失败
bool TcpClient::Socket_Init()
{
    // 创建流式套接字（TCP）
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    //第一个参数AF_INET 表示 IPv4 地址族
    //第二个参数SOCK_STREAM 表示流式套接字（TCP）
    //第三个参数0 表示使用默认协议，这里为 TCP
    if (sockfd == -1)
    {
        cout << "create socket err" << endl;//创建套接字失败
        return false;
    }

    // 构造服务器地址结构
    struct sockaddr_in saddr;//服务器地址结构体,我要连接哪个服务器的地址和端口号
    memset(&saddr, 0, sizeof(saddr));//将服务器地址结构体清零
    //初始化服务器地址结构体
    saddr.sin_family = AF_INET;                       // IPv4 地址族
    saddr.sin_port = htons(port);                      // 端口号需转网络字节序
    saddr.sin_addr.s_addr = inet_addr(ips.c_str());    // IP 地址字符串转整型

    // 主动连接服务器，阻塞三次握手
    int res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (res == -1)
    {
        cout << "connect ser err" << endl;//连接服务器失败
        return false;
    }
    return true;
}

// Run: 客户端主循环
// 不断打印菜单 -> 读取操作 -> switch 分发到对应业务函数，直到用户选择退出
void TcpClient::Run()
{
    while (running)//客户端主循环,直到用户选择退出，true时循环，false时退出
    {
        Print_info(); // 打印菜单并读取用户输入的 op_type
        switch (op_type)
        {
        case ZC:
            User_Register();
            break;
        case DL:
            User_Login();
            break;
        case CKYY:
            Show_Ticket();
            break;
        case YD:
            Yd_Ticket();
            break;
        case WDYY:
            Show_My_Ticket();
            break;
        case QXYY:
            Cancel_Ticket();
            break;
        case TC:
            running = false; // 用户选择退出，置运行标志为 false
            break;
        default:
            cout << "无效操作" << endl;
            break;
        }
    }
}

// Print_info: 打印用户菜单并读取用户输入的操作序号，结果存入 op_type
// 根据 login_status 区分两套菜单：
//   未登录菜单: 1 登录 / 2 注册 / 3 退出
//   已登录菜单: 1 查看预约 / 2 预定 / 3 我的预约 / 4 取消预约 / 5 退出
void TcpClient::Print_info()
{
    if (!login_status) // 未登录状态
    {
        cout << "--------用户名：游客--------状态：未登录--------" << endl;
        cout << " 1 登陆    2  注册    3 退出" << endl;
        cout << "---------------------------------------------" << endl;
        cout << "请输入要执行的操作序号：" << endl;
        if (!(cin >> op_type))
        {
            // 输入流失败（EOF 或非数字），清理状态并退出客户端
            cin.clear();//清理输入流状态
            running = false;
            op_type = TC;
            return;
        }

        // 用户输入 3 时对应退出，映射为 TC 枚举值
        if (op_type == 3)
        {
            op_type = TC;
        }
    }
    else // 已登录状态
    {
        cout << "--------用户名：" << username << "--------状态：已登陆--------" << endl;
        cout << "1 查看可预约的信息    2  预定  " << endl;
        cout << "3 查看我的预约       4 取消预约" << endl;
        cout << " 5 退出" << endl;
        cout << "请输入要执行的操作序号：" << endl;

        int input;//用户输入的操作序号
        if (!(cin >> input))//读取用户输入的操作序号
        {
            // 输入流失败（EOF 或非数字），清理状态并退出客户端
            cin.clear();//清理输入流状态
            running = false;
            op_type = TC;
            return;
        }
        // 将菜单序号映射为 OPTYPE 枚举值
        switch (input)
        {
        case 1:
            op_type = CKYY;
            break;
        case 2:
            op_type = YD;
            break;
        case 3:
            op_type = WDYY;
            break;
        case 4:
            op_type = QXYY;
            break;
        case 5:
            op_type = TC;
            break;
        default:
            op_type = -1; // 无效输入
            break;
        }
    }
}

// sendJson: 将 Json 对象序列化为单行字符串，末尾追加 \n 作为消息边界后发送到服务器
bool TcpClient::sendJson(const Json::Value &val)
{
    Json::FastWriter writer;//把Json 对象序列化为单行字符串，末尾追加 \n 作为消息边界后发送到服务器
    string json_str = writer.write(val);//将Json 对象序列化为单行字符串
    // FastWriter 末尾可能带换行，去掉后统一追加 \n 作为消息分隔符
    while (!json_str.empty() && (json_str.back() == '\n' || json_str.back() == '\r'))
        json_str.pop_back();
    //字符串不空且末尾是换行符或回车符，去掉末尾的换行符或回车符
    json_str += "\n";//在字符串末尾追加换行符 \n
    int n = send(sockfd, json_str.c_str(), json_str.length(), 0);
    //c_str()将字符串转换为C风格字符串
    return n == (int)json_str.length();
    //返回发送的字节数是否等于字符串的长度
}

// recvJson: 循环 recv 直到收到以 \n 结尾的完整消息，解析为 Json 对象
bool TcpClient::recvJson(Json::Value &res)
{
    string buf;
    char tmp[1024];
    // 阻塞循环读取，直到遇到 \n 或连接关闭
    while (true)
    {
        int n = recv(sockfd, tmp, sizeof(tmp) - 1, 0);
        //将读取到的字节数n转换为字符串tmp，并追加到buf中
        if (n <= 0)
            return false; // 连接关闭或出错
        buf.append(tmp, n);
        // 找到消息结束符 \n，停止读取
        if (buf.find('\n') != string::npos)
            break;
        //找到消息结束符 \n，停止读取
    }
    Json::Reader Read;//解析Json 字符串为Json 对象
    return Read.parse(buf, res);//解析buf中的Json 字符串为Json 对象res
}

// User_Register: 用户注册
// 读取手机号/用户名/密码，封装 JSON 发送给服务器，注册成功后自动置登录状态
void TcpClient::User_Register()
{
    cout << "请输入注册的手机号码" << endl;
    cin >> usertel;
    cout << "请输入用户名" << endl;
    cin >> username;
    string passwd;
    cout << "请输入密码" << endl;
    cin >> passwd;

    // 封装注册请求 JSON：type=ZC, 携带手机号/用户名/密码
    Json::Value val;
    val["type"] = ZC;
    val["user_tel"] = usertel;
    val["user_name"] = username;
    val["user_passwd"] = passwd;

    // 发送请求并接收响应
    if (!sendJson(val))//发送注册请求JSON字符串到服务器
    {
        cout << "发送失败" << endl;
        return;
    }
    Json::Value rval;
    if (!recvJson(rval))//接收服务器返回的注册响应JSON字符串
    {
        cout << "ser close" << endl;
        return;
    }

    // 服务器约定 status="OK" 表示成功
    string rstatus = rval["status"].asString();
    if (rstatus.compare("OK") != 0)
    {
        cout << "注册失败" << endl;
        return;
    }
    cout << "注册成功" << endl;
    login_status = true; // 注册成功自动登录
}

// User_Login: 用户登录
// 读取手机号/密码，发送 DL 请求，成功后保存服务器返回的用户名并置登录状态
void TcpClient::User_Login()
{
    cout << "请输入手机号码" << endl;
    cin >> usertel;
    cout << "请输入密码" << endl;
    string passwd;
    cin >> passwd;

    // 封装登录请求 JSON
    Json::Value val;
    val["type"] = DL;
    val["user_tel"] = usertel;
    val["user_passwd"] = passwd;

    // 发送请求并接收响应
    if (!sendJson(val))
    {
        cout << "发送失败" << endl;
        return;
    }
    Json::Value root;
    if (!recvJson(root))
    {
        cout << "ser close" << endl;
        return;
    }

    string s = root["status"].asString();
    if (s.compare("OK") != 0)
    {
        cout << "登录失败" << endl;
        return;
    }
    // 登录成功，保存服务器返回的用户名用于菜单显示
    username = root["user_name"].asString();
    login_status = true;
    cout << "登录成功" << endl;
}

// Show_Ticket: 查看所有可预约的车票信息列表
// 发送 CKYY 请求，解析服务器返回的 ticket_arr 数组并按表格打印
void TcpClient::Show_Ticket()
{
    // 封装查看请求 JSON（仅需 type=CKYY，无需额外参数）
    Json::Value val;//查看请求JSON对象  
    val["type"] = CKYY;

    // 发送请求并接收响应（recvJson 内部循环读取完整消息，不受缓冲区大小限制）
    if (!sendJson(val))
    {
        cout << "发送失败" << endl;
        return;
    }
    Json::Value res_val;//查看响应JSON对象  
    if (!recvJson(res_val))
    {
        cout << "ser close" << endl;
        return;
    }

    string s = res_val["status"].asString();
    //获取查看响应JSON对象中的status字段值
    if (s.compare("OK") != 0)//比较status字段值是否为OK
    {
        cout << "查看预约信息失败" << endl;
        return;
    }

    // num 为可预约车票总数
    int num = res_val["num"].asInt();
    if (num == 0)
    {
        cout << "暂时没有可预约的信息" << endl;
        return;
    }

    // 以表格形式打印车票信息：序号 / 名称 / 总票数 / 已预定 / 时间
    tk_ids.clear();
    cout << "|序号|   名称    |   总票数  |   已预定  |   时间    |" << endl;
    for (int i = 0; i < num; i++)
    {
        tk_ids.push_back(res_val["ticket_arr"][i]["ticket_id"].asInt());
        cout << "  " << (i + 1);
        cout << "      " << res_val["ticket_arr"][i]["ticket_name"].asString();
        cout << "      " << res_val["ticket_arr"][i]["ticket_max"].asString();
        cout << "      " << res_val["ticket_arr"][i]["ticket_count"].asString();
        cout << "      " << res_val["ticket_arr"][i]["day_time"].asString();
        cout << endl;
    }
}

// Yd_Ticket: 预定车票
// 先调用 Show_Ticket 展示列表，让用户输入序号，再发送 YD 请求
void TcpClient::Yd_Ticket()
{
    // 先展示可预约列表，供用户选择序号
    Show_Ticket();
    if (tk_ids.empty())
    {
        cout << "暂无可预定票务" << endl;
        return;
    }
    cout << "请输入要预定的序号" << endl;
    int index;
    cin >> index;
    if (index < 1 || index > (int)tk_ids.size())
    {
        cout << "序号错误" << endl;
        return;
    }

    // 将用户输入的列表序号转换为对应的 tk_id
    int tk_id = tk_ids[index - 1];

    // 封装预定请求 JSON：type=YD，携带用户手机号与车票 ID
    Json::Value val;
    val["type"] = YD;
    val["user_tel"] = usertel;
    val["ticket_id"] = to_string(tk_id);

    // 发送请求并接收响应
    if (!sendJson(val))
    {
        cout << "发送失败" << endl;
        return;
    }
    Json::Value res_val;
    if (!recvJson(res_val))
    {
        cout << "ser close" << endl;
        return;
    }

    string s = res_val["status"].asString();
    if (s.compare("OK") != 0)
    {
        cout << "预定失败" << endl;
        return;
    }
    cout << "预订成功" << endl;
}

// Show_My_Ticket: 查看当前用户的预约记录
// 发送 WDYY 请求（携带 user_tel），解析 my_ticket_arr 数组并打印
void TcpClient::Show_My_Ticket()
{
    // 封装请求 JSON：type=WDYY，携带用户手机号以查询其预约
    Json::Value val;
    val["type"] = WDYY;
    val["user_tel"] = usertel;

    // 发送请求并接收响应
    if (!sendJson(val))
    {
        cout << "发送失败" << endl;
        return;
    }
    Json::Value res_val;
    if (!recvJson(res_val))
    {
        cout << "ser close " << endl;
        return;
    }

    string s = res_val["status"].asString();
    if (s.compare("OK") != 0)
    {
        cout << "查看我的预约失败" << endl;
        return;
    }

    // num 为当前用户的预约记录数
    int num = res_val["num"].asInt();
    if (num == 0)
    {
        cout << "暂无预约信息" << endl;
        return;
    }

    // 以表格形式打印我的预约：序号 / 名称 / 总票数 / 已预定 / 日期 / 预约时间
    my_tk_ids.clear();
    cout << "|序号|   名称   | 总票数 | 已预定 |  日期  |  预约时间  |" << endl;
    for (int i = 0; i < num; i++)
    {
        my_tk_ids.push_back(res_val["my_ticket_arr"][i]["ticket_id"].asInt());
        cout << "  " << (i + 1);
        cout << "      " << res_val["my_ticket_arr"][i]["ticket_name"].asString();
        cout << "      " << res_val["my_ticket_arr"][i]["ticket_max"].asString();
        cout << "      " << res_val["my_ticket_arr"][i]["ticket_count"].asString();
        cout << "      " << res_val["my_ticket_arr"][i]["day_time"].asString();
        cout << "      " << res_val["my_ticket_arr"][i]["yd_time"].asString();
        cout << endl;
    }
}

// Cancel_Ticket: 取消预约
// 先调用 Show_My_Ticket 展示记录，让用户输入序号，再发送 QXYY 请求
void TcpClient::Cancel_Ticket()
{
    // 先展示我的预约记录，供用户选择要取消的序号
    Show_My_Ticket();
    if (my_tk_ids.empty())
    {
        cout << "暂无预约可取消" << endl;
        return;
    }
    cout << "请输入要取消的预约序号" << endl;
    int index;
    cin >> index;
    if (index < 1 || index > (int)my_tk_ids.size())
    {
        cout << "序号错误" << endl;
        return;
    }

    // 将用户输入的列表序号转换为对应的 tk_id
    int tk_id = my_tk_ids[index - 1];

    // 封装取消预约请求 JSON：type=QXYY，携带用户手机号与车票 ID
    Json::Value val;
    val["type"] = QXYY;
    val["user_tel"] = usertel;
    val["ticket_id"] = to_string(tk_id);

    // 发送请求并接收响应
    if (!sendJson(val))
    {
        cout << "发送失败" << endl;
        return;
    }
    Json::Value res_val;
    if (!recvJson(res_val))
    {
        cout << "ser close" << endl;
        return;
    }

    string s = res_val["status"].asString();
    if (s.compare("OK") != 0)
    {
        cout << "取消预约失败" << endl;
        return;
    }
    cout << "取消预约成功" << endl;
}

// main: 客户端程序入口
// 1. 创建 TcpClient 对象（使用默认构造连接 127.0.0.1:6000）
// 2. 初始化套接字并连接服务器，失败则退出
// 3. 进入主循环处理用户交互
int main()
{
    TcpClient cli;
    if (!cli.Socket_Init())
    {
        exit(1);
    }
    cli.Run();
}
