#ifndef SERVER_H
#define SERVER_H

/* ===================================================================
 *  票务预约系统 —— 服务器端头文件
 *  模块划分：
 *    1. SerConf      读取并保存服务器/数据库配置
 *    2. ThreadPool   基于环形队列的线程池，异步处理客户端请求
 *    3. MysqlClient  封装 MySQL 连接与所有业务 SQL（注册/登录/票务/预约）
 *    4. ConnPool     MySQL 连接池（单例），预创建连接供多线程复用
 *    5. ConnGuard    RAII 守卫，自动从连接池取/还连接
 *    6. Socket/LisSocket/ConSocket  epoll 模型下的套接字抽象
 *    7. TcpSer       服务器主控：socket + epoll + 线程池的组装与事件循环
 * =================================================================== */

#include <stdio.h>//标准输入输出头文件
#include <stdlib.h>//标准库头文件
#include <unistd.h>//系统调用头文件
#include <string.h>//字符串头文件
#include <sys/socket.h>//套接字头文件
#include <netinet/in.h>//IPv4 地址结构体
#include <arpa/inet.h>//网络地址转换函数
#include <iostream>//输入输出流头文件
#include <string>//字符串头文件
#include <queue>//队列头文件
#include <fcntl.h>//文件描述符控制函数
#include <sys/epoll.h>//epoll 模型下的事件通知函数
#include <pthread.h>//线程库函数
#include <jsoncpp/json/json.h>//JSON 库头文件
#include <mysql/mysql.h>//MySQL 库头文件
#include <atomic>//原子操作头文件（用于并发安全的连接数统计）

using namespace std;

// 全局当前存活客户端连接数（accept 成功后 +1，连接断开时 -1）
// 供高并发压测时实时观测"当前连接数"是否达到目标值，线程安全
extern std::atomic<long> g_alive;

const int EV_MAX = 128; // epoll_wait 一次最多返回的就绪事件数

// 客户端请求的操作类型（对应 JSON 中 type 字段的取值）
enum OPTYPE
{
    DL = 1,  // 登录
    ZC,      // 注册
    CKYY,    // 查看可预约票务
    YD,      // 预定票务
    WDYY,    // 查看我的预约
    QXYY,    // 取消预约
    TC       // 退出
};

/* ===================================================================
 *  SerConf：服务器配置类
 *  负责从 my.conf 读取配置项，提供默认值兜底
 * =================================================================== */
class SerConf
{
public:
    // 默认构造：填充各项默认值，配置文件缺失时仍可运行
    SerConf()
    {
        ips = "127.0.0.1";
        port = 6000;
        lismax = 1024;
        taskmax = 1024;
        threadnum = 4;
        db_ip = "127.0.0.1";
        db_port = 3306;
        db_name = "2501project";
        db_username = "root";
        db_passwd = "111111";
        pool_size = 8;
    }

    // 读取配置文件
    // 参数 filename: 配置文件路径（如 "my.conf"）
    // 返回值: true 读取成功；false 文件打开失败
    bool ReadConf(string filename);

    // 打印当前所有配置项到标准输出（调试用）
    void PrintInfo();

public:
    string ips;          // 服务器监听 IP
    short port;          // 服务器监听端口
    int lismax;          // listen 监听队列的最大长度
    int taskmax;         // 线程池任务队列的最大长度
    int threadnum;       // 线程池中工作线程的数量
    string db_ip;        // 数据库 IP
    short db_port;       // 数据库端口
    string db_name;      // 数据库名
    string db_username;  // 数据库用户名
    string db_passwd;    // 数据库密码
    int pool_size;       // 数据库连接池大小
};

class Socket; // 前向声明，供 task_t 使用

// 线程池任务结构体：封装一个待处理的客户端套接字对象
typedef struct
{
    Socket *con; // 指向具体的客户端套接字对象
} task_t;

/* ===================================================================
 *  ThreadPool：线程池类
 *  使用环形数组作为任务队列，互斥锁 + 条件变量保证线程安全
 * =================================================================== */
class ThreadPool
{
public:
    // 构造函数
    // 参数 num: 工作线程数量；task_num: 任务队列最大容量
    ThreadPool(int num, int task_num) : thread_num(num), max_queue(task_num)
    {
    }

    // 初始化线程池：分配任务队列内存、初始化互斥锁与条件变量
    // 返回值: true 成功；false 内存分配失败
    bool Thread_Pool_Init();

    // 向任务队列中添加一个任务（客户端套接字）
    // 参数 csocket: 待处理的客户端 Socket 对象指针；队列满时直接 delete 丢弃
    void Add_Task(Socket *csocket);

    // 创建并启动所有工作线程（线程以 detach 方式运行，自动回收）
    void Start_Thread();

    // 工作线程主循环：阻塞等待任务，取出后调用 Socket::Handle_Data()
    void Work();

private:
    task_t *queue;          // 环形任务队列数组
    int front;              // 队头指针（取任务位置）
    int rear;               // 队尾指针（放任务位置）
    int count;              // 当前队列中的任务数量
    int stop;               // 线程退出标志：0=运行中 1=退出
    int max_queue;          // 任务队列最大容量
    int thread_num;         // 工作线程数量
    pthread_mutex_t mutex;  // 保护任务队列的互斥锁
    pthread_cond_t cond;    // 条件变量：无任务时线程在此阻塞等待
};

/* ===================================================================
 *  MysqlClient：MySQL 数据库操作类
 *  封装一条 MySQL 连接及所有业务 SQL 操作（由连接池统一管理生命周期）
 * =================================================================== */
class MysqlClient
{
public:
    // 默认构造：使用硬编码的数据库连接参数
    MysqlClient()
    {
        db_ip = "127.0.0.1";
        db_port = 3306;
        db_name = "2501project";
        db_username = "root";
        db_passwd = "111111";
    }

    // 带参构造：由连接池使用，传入具体数据库参数
    // 参数 ip/port/dbname/dbusername/passwd: 数据库连接信息
    MysqlClient(string ip, short port, string dbname, string dbusername, string passwd)
    {
        db_ip = ip;
        db_port = port;
        db_name = dbname;
        db_username = dbusername;
        db_passwd = passwd;
    }

    // 析构：关闭 MySQL 连接
    ~MysqlClient()
    {
        mysql_close(&mysql_con);
    }

    // 连接到 MySQL 服务器
    // 返回值: true 连接成功；false 连接失败（错误信息打印到 stdout）
    bool Connect_MysqlServer();

    // 检测当前连接是否存活
    // 返回值: true 连接正常；false 连接已断开
    bool Ping();

    // 断开旧连接后重新建立连接（连接池发现连接失效时调用）
    // 返回值: true 重连成功；false 重连失败
    bool ReConnect();

    // 用户注册：向 user_info 表插入一条用户记录
    // 参数 tel: 手机号；name: 用户名；passwd: 密码
    // 返回值: true 插入成功；false SQL 执行失败
    bool Db_User_Register(const string &tel, const string &name, const string &passwd);

    // 用户登录：根据手机号和密码查询用户名
    // 参数 tel: 手机号；name(出参): 返回查到的用户名；passwd: 密码
    // 返回值: true 查到唯一一条记录；false 查询失败或记录数不为 1
    bool Db_User_Login(const string &tel, string &name, const string &passwd);

    // 查看所有票务：查询 ticket_table 全部记录，填入 res
    // 参数 res(出参): Json 对象，含 status/num/ticket_arr
    // 返回值: true 查询成功；false SQL 执行失败
    bool Db_Show_Ticket(Json::Value &res);

    // 预定票务：检查余量 → 更新已售数量 → 插入预约记录（事务）
    // 参数 usertel: 用户手机号；tk_id: 票务 ID
    // 返回值: true 预定成功；false 票已售罄或 SQL 失败（自动回滚）
    bool Db_Yd_Ticket(string usertel, string tk_id);

    // 查看我的预约：查询指定用户 status=1 的预约记录
    // 参数 usertel: 用户手机号；res(出参): Json 对象
    // 返回值: true 查询成功；false SQL 执行失败
    bool Db_Show_My_Ticket(const string &usertel, Json::Value &res);

    // 取消预约：将 yd_table 对应记录 status 置 0，并回退票务已售数量（事务）
    // 参数 usertel: 用户手机号；tk_id: 票务 ID
    // 返回值: true 取消成功；false 记录不存在或 SQL 失败（自动回滚）
    bool Db_Cancel_Ticket(const string &usertel, const string &tk_id);

    // 开启事务
    void Mysql_Begin();
    // 回滚事务
    void Mysql_RollBack();
    // 提交事务
    void Mysql_Commit();

private:
    string db_ip;        // 数据库 IP
    short db_port;       // 数据库端口
    string db_name;      // 数据库名
    string db_username;  // 数据库用户名
    string db_passwd;    // 数据库密码
    MYSQL mysql_con;     // MySQL 连接句柄
};

/* ===================================================================
 *  ConnPool：MySQL 连接池（单例）
 *  预创建 poolSize 条连接放入空闲队列，多线程通过 get/return 复用
 *  线程安全：互斥锁 + 条件变量；含连接存活检测与失效重连
 * =================================================================== */
class ConnPool
{
public:
    // 获取全局唯一连接池实例（懒汉式单例）
    static ConnPool *getInstance();
    //当只有第一次有人来要实例时，才会创建实例，后续调用直接返回实例

    // 初始化连接池：创建 poolSize 条连接并放入空闲队列
    // 参数 ip/port/dbname/user/passwd: 数据库连接信息；poolSize: 连接池大小
    // 返回值: true 全部连接创建成功；false 任一连接创建失败（已回滚）
    bool init(const string &ip, short port, const string &dbname,
              const string &user, const string &passwd, int poolSize);

    // 从池中取一条连接；队列空则阻塞等待，直到有连接归还或池被销毁
    // 返回值: 可用的 MysqlClient*；池销毁时返回 nullptr
    MysqlClient *getConnection();

    // 归还连接到池中，并唤醒一个等待中的取连接线程
    // 参数 conn: 用完的连接指针
    void returnConnection(MysqlClient *conn);

    // 销毁池中所有连接（服务器退出时调用）
    void destroy();

    // 获取当前空闲连接数（监控/调试用）
    int getIdleCount();

private:
    ConnPool();                              // 私有构造
    ~ConnPool();                             // 私有析构
    ConnPool(const ConnPool &) = delete;     // 禁止拷贝
    ConnPool &operator=(const ConnPool &) = delete; // 禁止赋值

private:
    queue<MysqlClient *> m_connQueue; // 空闲连接队列
    pthread_mutex_t m_mutex;          // 保护队列的互斥锁
    pthread_cond_t m_cond;            // 连接归还时唤醒等待线程
    int m_poolSize;                   // 连接池总大小
    bool m_isStop;                    // 销毁标志
    string m_ip;                      // 数据库 IP（用于重建失效连接）
    short m_port;                     // 数据库端口
    string m_dbname;                  // 数据库名
    string m_user;                    // 数据库用户名
    string m_passwd;                  // 数据库密码
    static ConnPool *m_instance;      // 单例实例
};

/* ===================================================================
 *  ConnGuard：连接守卫（RAII）在构造时从连接池取连接，析构时自动归还，防止连接泄漏
 *  构造时从连接池取连接，析构时自动归还，防止连接泄漏
 *  使用方式：ConnGuard cli; cli->Db_XXX(...);
 * =================================================================== */
class ConnGuard
{
public:
    // 构造：调用 ConnPool::getConnection() 获取一条连接
    ConnGuard() : m_conn(ConnPool::getInstance()->getConnection()) {}

    // 析构：将连接归还到连接池
    ~ConnGuard()
    {
        if (m_conn != nullptr)
            ConnPool::getInstance()->returnConnection(m_conn);
    }

    // 重载 operator->，使 ConnGuard 可像 MysqlClient* 一样使用
    MysqlClient *operator->() { return m_conn; }

    // 获取原始连接指针
    MysqlClient *get() { return m_conn; }

    // 连接是否获取成功
    bool valid() const { return m_conn != nullptr; }

private:
    ConnGuard(const ConnGuard &) = delete;            // 禁止拷贝
    ConnGuard &operator=(const ConnGuard &) = delete;  // 禁止赋值
    MysqlClient *m_conn; // 持有的连接指针
};

/* ===================================================================
 *  Socket：套接字基类（抽象）
 *  统一封装文件描述符与 epoll 句柄，子类实现 Handle_Data
 * =================================================================== */
class Socket
{
public:
    // 构造：绑定套接字 fd 与所属 epoll 句柄
    Socket(int fd, int epfd) : m_fd(fd), m_epfd(epfd)
    {
    }

    // 析构：关闭套接字文件描述符
    virtual ~Socket()
    {
        close(m_fd);
    }

    // 纯虚函数：处理该套接字上的数据，子类必须实现
    virtual void Handle_Data() = 0;

public:
    int m_fd;   // 套接字文件描述符
    int m_epfd; // 该套接字所属的 epoll 内核事件表句柄
};

/* ===================================================================
 *  LisSocket：监听套接字
 *  专门处理客户端连接请求（accept），创建 ConSocket 并加入 epoll
 * =================================================================== */
class LisSocket : public Socket
{
public:
    LisSocket(int fd, int epfd) : Socket(fd, epfd)
    {
    }

    // 处理监听 socket 的可读事件：accept 新连接
    void Handle_Data();

private:
    // 重新注册 epoll 事件（EPOLLONESHOT 触发后需重新激活）
    void RestEvent();
};

/* ===================================================================
 *  ConSocket：客户端连接套接字
 *  处理单个客户端的数据收发与业务逻辑分发
 * =================================================================== */
class ConSocket : public Socket
{
public:
    ConSocket(int fd, int epfd) : Socket(fd, epfd)
    {
        m_OpType = -1; // 初始化为无效操作类型
    }

    // 处理客户端数据：接收 → 解析 type → 分发到对应业务函数
    void Handle_Data();

private:
    // 重新注册 epoll 事件（EPOLLONESHOT）
    void RestEvent();

    // 解析客户端 JSON 请求，提取 type 字段存入 m_OpType
    // 参数 buff: 客户端发来的原始字符串
    void Get_OpType(char *buff);

    // 向客户端发送 {"status":"OK"}
    void Send_Ok();

    // 向客户端发送 {"status":"ERR"}
    void Send_Err();

    // 向客户端发送一个 Json 对象（用于返回查询数据）
    // 参数 root: 待发送的 Json::Value
    void Send_JsonObj(Json::Value &root);

    // 处理用户注册请求
    void User_Register();
    // 处理用户登录请求
    void User_Login();
    // 处理查看所有可预约票务请求
    void Show_Ticket();
    // 处理预定票务请求
    void Yd_Ticket();
    // 处理查看我的预约请求
    void Show_My_Ticket();
    // 处理取消预约请求
    void Cancel_Ticket();

private:
    Json::Value m_val;   // 解析后的客户端请求 JSON 对象
    int m_OpType;        // 请求操作类型（对应 OPTYPE 枚举）
    string m_recvBuf;    // 接收缓冲区：累积跨多次 recv 的数据，按 \n 分包
};

/* ===================================================================
 *  TcpSer：TCP 服务器主控类
 *  组装监听 socket + epoll + 线程池，运行事件循环
 * =================================================================== */
class TcpSer
{
public:
    // 构造：用配置对象初始化线程池
    // 参数 sc: 服务器配置对象
    TcpSer(SerConf &sc) : m_conf(sc), m_pool(sc.threadnum, sc.taskmax)
    {
        m_sockfd = -1;
        m_epfd = -1;
    }

    // 服务器初始化：创建监听 socket 并创建 epoll 句柄
    // 返回值: true 成功；false 失败
    bool Ser_Init();

    // 服务器主循环：初始化 → 加入监听 → 启动线程池 → epoll_wait 循环
    void Run();

private:
    // 创建并绑定监听 socket，开始监听
    // 返回值: true 成功；false 失败
    bool create_socket();

    // 处理 epoll 返回的就绪事件：将读事件对应的 Socket 加入线程池队列
    void do_events();

private:
    SerConf m_conf;                  // 服务器配置对象
    int m_sockfd;                    // 监听套接字文件描述符
    int m_epfd;                      // epoll 内核事件表句柄
    int m_num;                       // epoll_wait 返回的就绪事件数
    struct epoll_event m_evs[EV_MAX]; // 存储 epoll_wait 返回的就绪事件
    ThreadPool m_pool;               // 线程池对象
};

#endif // SERVER_H
