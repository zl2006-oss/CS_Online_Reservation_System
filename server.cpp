#include "server.h"

/************************************************************
 *  SerConf 类实现
 ************************************************************/

// 读取配置文件
// 逐行解析 "key value" 格式，忽略以 # 开头的注释行和空行
// 参数 filename: 配置文件路径
// 返回值: true 成功；false 文件打开失败
bool SerConf::ReadConf(string filename)
{
    FILE *fp = fopen(filename.c_str(), "r");//打开配置文件
    //让运维人员修改参数的时候，不修改源代码，不需要重新变异，降低出错的封信
    //如果文件打开失败，返回false
    if (fp == nullptr)//如果文件指针为空，说明文件打开失败
    {
        cout << "open file " << filename << " err!" << endl;
        return false;
    }
    char buff[128]{0};
    int num = 1; // 记录行号，便于错误定位

    // 逐行读取配置文件
    while (fgets(buff, 127, fp) != NULL)//如果读取到内容，说明文件未读取完
    {
        // 跳过注释行（以 # 开头）和空行
        if (strncmp(buff, "#", 1) == 0 || strncmp(buff, "\n", 1) == 0)
        {
            num++;
            continue;
        }
        // 去掉行末的换行符
        if (buff[strlen(buff) - 1] == '\n')
        {
            buff[strlen(buff) - 1] = '\0';
        }
        // 按空格拆分出 key 和 value
        char *k = strtok(buff, " ");
        char *v = strtok(NULL, " ");
        if (k == NULL || v == NULL)
        {
            cout << "文件第：" << num << "行无法解析" << endl;
            num++;
            continue;
        }
        // 根据 key 设置对应的成员变量
        if (strcmp(k, "ip") == 0)
            ips = v;
        else if (strcmp(k, "port") == 0)
            port = atoi(v);
        else if (strcmp(k, "lismax") == 0)
            lismax = atoi(v);
        else if (strcmp(k, "taskmax") == 0)
            taskmax = atoi(v);
        else if (strcmp(k, "threadnum") == 0)
            threadnum = atoi(v);
        else if (strcmp(k, "db_ip") == 0)
            db_ip = v;
        else if (strcmp(k, "db_port") == 0)
            db_port = atoi(v);
        else if (strcmp(k, "db_name") == 0)
            db_name = v;
        else if (strcmp(k, "db_username") == 0)
            db_username = v;
        else if (strcmp(k, "db_passwd") == 0)
            db_passwd = v;
        else if (strcmp(k, "pool_size") == 0)
            pool_size = atoi(v);
        else
            cout << "无效k:" << k << endl;
        num++;
    }
    fclose(fp);
    return true;
}

// 打印当前所有配置项到标准输出（调试用）
void SerConf::PrintInfo()
{
    cout << "----Serconf----" << endl;
    cout << "ip: " << ips << endl;
    cout << "port: " << port << endl;
    cout << "lismax:" << lismax << endl;
    cout << "tasknum:" << taskmax << endl;
    cout << "threadnum:" << threadnum << endl;
    cout << "db_ip: " << db_ip << endl;
    cout << "db_port: " << db_port << endl;
    cout << "db_name: " << db_name << endl;
    cout << "db_username: " << db_username << endl;
    cout << "pool_size: " << pool_size << endl;
    cout << "-------------" << endl;
}

/************************************************************
 *  ThreadPool 类实现 线程池类的实现
 *  基于环形数组的任务队列，工作线程通过条件变量等待任务
 ************************************************************/

// 工作线程入口函数（pthread_create 需要的全局函数）
// 参数 arg: 指向 ThreadPool 对象的指针
void *worker_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;//将 arg 转换为 ThreadPool 类型的指针

    pool->Work();//调用工作线程的 Work 方法
    return NULL;
}

// 初始化线程池：分配任务队列内存，初始化互斥锁和条件变量
// 返回值: true 成功；false 内存分配失败
bool ThreadPool::Thread_Pool_Init()
{
    queue = (task_t *)malloc(sizeof(task_t) * max_queue);//分配任务队列内存
    //如果内存分配失败，返回false
    if (queue == nullptr)
        return false;
    front = 0;
    rear = 0;
    count = 0;
    stop = 0;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    return true;
}

// 向任务队列中添加一个新任务
// 参数 csocket: 待处理的客户端 Socket 对象；队列满时直接 delete 丢弃
//Socket对象：客户端套接字对象，用于与客户端进行通信
void ThreadPool::Add_Task(Socket *csocket)
{
    pthread_mutex_lock(&mutex);//获取互斥锁，确保线程安全，加锁
    if (count >= max_queue)//如果队列已满
    {
        delete csocket; // 队列已满，丢弃该任务
        pthread_mutex_unlock(&mutex);
        return;
    }
    queue[rear].con = csocket;//将客户端套接字对象添加到队列尾部
    rear = (rear + 1) % max_queue; // 环形队列：队尾指针循环前进
    count++;//任务数量增加
    pthread_cond_signal(&cond); // 唤醒一个等待任务的工作线程
    pthread_mutex_unlock(&mutex);//解锁互斥锁
}

// 启动所有工作线程（detach 方式，线程结束自动回收资源）
void ThreadPool::Start_Thread()
{
    pthread_t ids[thread_num];//创建线程数组，用于存储所有工作线程的线程 ID
    // 遍历所有工作线程，创建并分离线程
    // 每个工作线程执行 worker_thread 函数，将 this 指针作为参数传递
    for (int i = 0; i < thread_num; i++)//遍历所有工作线程，创建并分离线程
    {
        pthread_create(&ids[i], NULL, worker_thread, this);
        //创建线程，执行 worker_thread 函数，将 this 指针作为参数传递
        pthread_detach(ids[i]);
        //分离线程，线程结束时自动回收资源
    }
}

// 工作线程主循环：阻塞等待任务，取出后调用 Handle_Data 处理
void ThreadPool::Work()
{
    while (true)//无限循环，持续处理任务
    // 每次循环获取互斥锁，确保线程安全，加锁，处理任务
    {
        pthread_mutex_lock(&mutex);//获取互斥锁，确保线程安全，加锁 
        // 无任务且未停止时，在条件变量上阻塞等待
        while (count == 0 && !stop)
            pthread_cond_wait(&cond, &mutex);
        // 收到停止信号且队列已空，退出线程
        if (stop && count == 0)
        {
            pthread_mutex_unlock(&mutex);
            break;
        }
        // 取出队首任务
        Socket *con = queue[front].con;
        front = (front + 1) % max_queue;
        count--;
        pthread_mutex_unlock(&mutex);
        if (con != nullptr)
            con->Handle_Data(); // 执行客户端请求处理
    }
    pthread_exit(NULL);//退出线程，返回 NULL
}

/************************************************************
 *  LisSocket 类实现 监听套接字类的实现
 *  监听套接字：accept 新连接，创建 ConSocket 并加入 epoll
 ************************************************************/

// 全局当前存活连接数定义（server.h 中 extern 声明，accept 成功 +1，断开 -1）
std::atomic<long> g_alive(0);

// 重新注册 epoll 事件（EPOLLONESHOT 模式下每次触发后必须重新激活）
void LisSocket::RestEvent()
{
    struct epoll_event ev;//创建 epoll 事件结构体，用于存储监听 socket 的事件信息
    ev.data.ptr = this;
    //将 this 指针赋值给 epoll 事件结构体的 data 字段，用于后续处理
    ev.events = EPOLLIN | EPOLLONESHOT;
    //将 EPOLLIN 事件和 EPOLLONESHOT 事件赋值给 epoll 事件结构体的 events 字段
    //EPOLLONESHOT 事件：监听 socket 只读一次，触发后立即移除监听
    //将 epoll 事件结构体 ev 赋值给 epoll_ctl 函数的参数，用于修改 epoll 监听的事件
    epoll_ctl(m_epfd, EPOLL_CTL_MOD, m_fd, &ev);
}

// 处理监听 socket 的可读事件：accept 新连接
// 高并发改造：循环 accept 直到取空内核 backlog（EAGAIN），一次性收下所有已到达的连接
// 每 accept 成功一个就 +1 全局存活计数，并按 1 万为档打印进度，便于观测是否达到目标
void LisSocket::Handle_Data()
{
    while (true) // 循环取空 backlog，避免"一次只 accept 一个"导致建连吞吐成为瓶颈
    {
        int c = accept(m_fd, NULL, NULL);
        if (c < 0) // 取空或出错（EAGAIN/EMFILE），退出循环
            break;
        g_alive++; // 当前存活连接数 +1
        // 为新连接创建 ConSocket 对象并加入 epoll 监听
        ConSocket *cs = new ConSocket(c, m_epfd);
        struct epoll_event ev;
        ev.data.ptr = cs;
        ev.events = EPOLLIN | EPOLLONESHOT;
        epoll_ctl(m_epfd, EPOLL_CTL_ADD, c, &ev);
        // 每完成 1 万个连接打印一次进度（十万目标下共打印约 10 次，避免刷屏）
        long now = g_alive.load();
        if (now % 10000 == 0)
            cout << "[压测] 当前存活连接数: " << now << endl;
    }
    RestEvent(); // 重新激活监听 socket 的 epoll 事件，等待下一批新连接
}

/************************************************************
 *  MysqlClient 类实现
 *  封装一条 MySQL 连接及所有业务 SQL 操作
 ************************************************************/

// 连接到 MySQL 服务器（mysql_init + mysql_real_connect）
// 返回值: true 成功；false 失败（错误信息打印到 stdout）
bool MysqlClient::Connect_MysqlServer()
{
    if (mysql_init(&mysql_con) == NULL)//初始化 MySQL 连接 失败返回 NULL
        return false;
    if (mysql_real_connect(&mysql_con, db_ip.c_str(), db_username.c_str(),
                           db_passwd.c_str(), db_name.c_str(), db_port, NULL, 0) == NULL)
    {
        cout << "数据库连接失败: " << mysql_error(&mysql_con) << endl;
        return false;
    }
    //连接成功，返回 true
    return true;
}

// 检测连接是否存活
// 返回值: true 连接正常；false 连接已断开
bool MysqlClient::Ping()
{
    return mysql_ping(&mysql_con) == 0;
}

// 连接失效时重新连接：先关闭旧连接，再重新建立
// 返回值: true 重连成功；false 重连失败
bool MysqlClient::ReConnect()
{
    mysql_close(&mysql_con);//关闭旧连接
    //重新连接 MySQL 服务器
    return Connect_MysqlServer();
}

// 用户注册：向 user_info 表插入一条用户记录（Status=1，注册日期=当前日期）
// 参数 tel: 手机号；name: 用户名；passwd: 密码
// 返回值: true 插入成功；false SQL 执行失败
bool MysqlClient::Db_User_Register(const string &tel, const string &name, const string &passwd)
{
    string sql = "insert into user_info (Tel, Name, Password, Status, Ztime) "
                 "values('" +
                 tel + "','" + name + "','" + passwd + "',1,curdate())";
    if (mysql_query(&mysql_con, sql.c_str()) != 0)//执行 SQL 语句失败
        return false;
    return true;
}

// 用户登录：根据手机号和密码查询用户名
// 参数 tel: 手机号；name(出参): 返回查到的用户名；passwd: 密码
// 返回值: true 查到唯一一条记录；false 查询失败或记录数不为 1
bool MysqlClient::Db_User_Login(const string &tel, string &name, const string &passwd)
{
    string sql = "select Name from user_info where Tel='" + tel + "' and Password='" + passwd + "'";
    if (mysql_query(&mysql_con, sql.c_str()) != 0)
        return false;
    MYSQL_RES *r = mysql_store_result(&mysql_con);//存储查询结果
    if (r == NULL)
        return false;
    int num = (int)mysql_num_rows(r);//获取查询结果的行数
    if (num != 1)
    {
        mysql_free_result(r);//释放查询结果
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(r);//获取查询结果的第一行
    name = row[0];//将用户名赋值给 name
    mysql_free_result(r);//释放查询结果
    return true;
}

// 查看所有票务：查询 ticket_table 全部记录
// 参数 res(出参): Json 对象，含 status/num/ticket_arr
// 返回值: true 查询成功；false SQL 执行失败
bool MysqlClient::Db_Show_Ticket(Json::Value &res)
{
    string sql = "select tk_id,tk_name,tk_max,tk_count,datetime from ticket_table";
    if (mysql_query(&mysql_con, sql.c_str()) != 0)//执行 SQL 语句失败
        return false;
    MYSQL_RES *r = mysql_store_result(&mysql_con);//存储查询结果
    if (r == nullptr)
        return false;
    int num = (int)mysql_num_rows(r);//获取查询结果的行数
    res["status"] = "OK";
    res["num"] = num;
    if (num == 0)
    {
        mysql_free_result(r);
        return true;
    }
    // 逐行读取，每条票务封装为一个 Json 对象追加到 ticket_arr
    for (int i = 0; i < num; i++)
    {
        Json::Value tmp;
        MYSQL_ROW row = mysql_fetch_row(r);
        tmp["ticket_id"] = row[0];
        tmp["ticket_name"] = row[1];
        tmp["ticket_max"] = row[2];
        tmp["ticket_count"] = row[3];
        tmp["day_time"] = row[4];
        res["ticket_arr"].append(tmp);
    }
    mysql_free_result(r);
    return true;
}

// 预定票务：使用原子 UPDATE 扣减库存，再插入预约记录（事务）
// 参数 usertel: 用户手机号；tk_id: 票务 ID
// 返回值: true 预定成功；false 票已售罄或 SQL 失败（自动回滚）
bool MysqlClient::Db_Yd_Ticket(string usertel, string tk_id)
{
    Mysql_Begin(); // 开启事务

    // 原子扣减库存：仅当 tk_count < tk_max 时才 +1
    // InnoDB 行锁保证该 UPDATE 并发安全，避免"先查后改"的超卖竞态
    string sql_setcount = "update ticket_table set tk_count=tk_count+1 "
                          "where tk_id=" + tk_id + " and tk_count < tk_max";
    if (mysql_query(&mysql_con, sql_setcount.c_str()) != 0)
    {
        Mysql_RollBack();
        return false;
    }//执行 SQL 语句失败
    // 检查是否成功扣到了最后一张票
    // affected_rows == 1 表示抢到了最后一张票；== 0 表示票已售罄或票务不存在
    if (mysql_affected_rows(&mysql_con) != 1)
    {
        Mysql_RollBack();//回滚事务
        return false;
    }

    // 插入预约记录
    string sql_yd = "insert into yd_table (tel, tk_id, ctime, status) "
                    "values('" +
                    usertel + "'," + tk_id + ",now(),1)";
    if (mysql_query(&mysql_con, sql_yd.c_str()) != 0)
    {
        Mysql_RollBack();
        return false;
    }

    Mysql_Commit(); // 提交事务
    return true;
}

// 查看我的预约：查询指定用户 status=1 的有效预约记录
// 参数 usertel: 用户手机号；res(出参): Json 对象
// 返回值: true 查询成功；false SQL 执行失败
bool MysqlClient::Db_Show_My_Ticket(const string &usertel, Json::Value &res)
{
    string sql = "select t.tk_id,t.tk_name,t.tk_max,t.tk_count,t.datetime,y.ctime "
                 "from yd_table y left join ticket_table t on y.tk_id = t.tk_id "
                 "where y.tel='" +
                 usertel + "' and y.status=1";
    if (mysql_query(&mysql_con, sql.c_str()) != 0)
        return false;
    MYSQL_RES *r = mysql_store_result(&mysql_con);//存储查询结果
    if (r == nullptr)
        return false;
    res["status"] = "OK";
    res["num"] = (int)mysql_num_rows(r);//获取查询结果的行数
    if (res["num"].asInt() == 0)
    {
        mysql_free_result(r);//释放查询结果
        return true;
    }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(r)) != NULL)//逐行读取查询结果
    {
        Json::Value tmp;
        tmp["ticket_id"] = atoi(row[0]);
        tmp["ticket_name"] = row[1];
        tmp["ticket_max"] = atoi(row[2]);
        tmp["ticket_count"] = atoi(row[3]);
        tmp["day_time"] = row[4];
        tmp["yd_time"] = row[5];
        res["my_ticket_arr"].append(tmp);
    }
    mysql_free_result(r);//释放查询结果
    return true;
}

// 取消预约：将 yd_table 记录 status 置 0，并回退票务已售数量（事务）
// 参数 usertel: 用户手机号；tk_id: 票务 ID
// 返回值: true 取消成功；false 记录不存在或 SQL 失败（自动回滚）
bool MysqlClient::Db_Cancel_Ticket(const string &usertel, const string &tk_id)
{
    // 先检查是否存在有效的预约记录
    string sql_check = "select 1 from yd_table where tel='" + usertel + "' and tk_id=" + tk_id + " and status=1";
    if (mysql_query(&mysql_con, sql_check.c_str()) != 0)
        return false;
    MYSQL_RES *r = mysql_store_result(&mysql_con);//存储查询结果
    if (r == nullptr || (int)mysql_num_rows(r) == 0)
    {
        if (r)
            mysql_free_result(r);//释放查询结果
        return false;
    }
    mysql_free_result(r);//释放查询结果 

    Mysql_Begin(); // 开启事务
    // 将预约记录标记为已取消
    string sql_update_yd = "update yd_table set status=0 where tel='" + usertel + "' and tk_id=" + tk_id;
    if (mysql_query(&mysql_con, sql_update_yd.c_str()) != 0)
    {
        Mysql_RollBack();
        return false;
    }
    // 回退票务已售数量
    string sql_update_ticket = "update ticket_table set tk_count=tk_count-1 where tk_id=" + tk_id;
    if (mysql_query(&mysql_con, sql_update_ticket.c_str()) != 0)
    {
        Mysql_RollBack();
        return false;
    }
    Mysql_Commit(); // 提交事务
    return true;
}

// 开启事务
void MysqlClient::Mysql_Begin()
{
    mysql_query(&mysql_con, "begin");
}

// 回滚事务
void MysqlClient::Mysql_RollBack()
{
    mysql_query(&mysql_con, "rollback");
}

// 提交事务
void MysqlClient::Mysql_Commit()
{
    mysql_query(&mysql_con, "commit");
}

/************************************************************
 *  ConnPool 类实现
 *  MySQL 连接池（单例）：预创建连接、线程安全获取/归还、失效重连
 ************************************************************/

ConnPool *ConnPool::m_instance = nullptr; // 单例实例指针
//m_instance 是一个静态成员变量，用于存储连接池的单例实例指针
// 获取全局唯一连接池实例（懒汉式：首次调用时创建）
ConnPool *ConnPool::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new ConnPool();
    return m_instance;
}

// 构造：初始化成员与同步原语
ConnPool::ConnPool()
{
    m_poolSize = 0;
    m_port = 3306; // 默认 MySQL 端口
    m_isStop = false;
    pthread_mutex_init(&m_mutex, NULL);
    pthread_cond_init(&m_cond, NULL);
}

// 析构：销毁所有连接并释放同步原语
ConnPool::~ConnPool()
{
    destroy();
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

// 初始化连接池：创建 poolSize 条连接并放入空闲队列
// 参数 ip/port/dbname/user/passwd: 数据库连接信息；poolSize: 连接池大小
// 返回值: true 全部成功；false 任一连接创建失败（已回滚已创建的连接）
bool ConnPool::init(const string &ip, short port, const string &dbname,
                    const string &user, const string &passwd, int poolSize)
{
    // 保存数据库配置，供后续重建失效连接时使用
    m_ip = ip;
    m_port = port;
    m_dbname = dbname;
    m_user = user;
    m_passwd = passwd;
    m_poolSize = poolSize;

    for (int i = 0; i < poolSize; i++)//创建多个连接，每个连接都连接到数据库
    {
        MysqlClient *conn = new MysqlClient(ip, port, dbname, user, passwd);
        if (!conn->Connect_MysqlServer())//连接数据库
        {
            delete conn;
            cout << "连接池初始化失败：第 " << i + 1 << " 条连接创建失败" << endl;
            destroy(); // 回滚：销毁已创建的连接
            return false;
        }
        m_connQueue.push(conn);//将连接放入空闲队列
    }
    cout << "连接池初始化成功，共创建 " << poolSize << " 条连接" << endl;
    return true;
}

// 从池中取一条连接；队列空则阻塞等待
// 返回值: 可用的 MysqlClient*；池被销毁时返回 nullptr
MysqlClient *ConnPool::getConnection()
{
    pthread_mutex_lock(&m_mutex);
    // 队列为空且未停止时，阻塞等待归还连接
    while (m_connQueue.empty() && !m_isStop)//等待归还连接
        pthread_cond_wait(&m_cond, &m_mutex);
    if (m_isStop)//如果连接池被销毁，返回 nullptr
    {
        pthread_mutex_unlock(&m_mutex);
        return nullptr;
    }
    // 取出队首连接
    MysqlClient *conn = m_connQueue.front();
    m_connQueue.pop();
    pthread_mutex_unlock(&m_mutex);

    // 健康检查：MySQL wait_timeout 可能导致长连接被服务端断开
    if (conn != nullptr && !conn->Ping())
    {
        cout << "检测到失效连接，尝试重连..." << endl;
        if (!conn->ReConnect())
        {
            // 重连失败：用保存的配置新建一条连接替换，尽量维持池大小
            cout << "重连失败，创建新连接替换" << endl;
            delete conn;
            conn = new MysqlClient(m_ip, m_port, m_dbname, m_user, m_passwd);//创建新连接
            if (!conn->Connect_MysqlServer())//连接数据库
            {
                delete conn;
                return nullptr; // 数据库不可用，返回 null 由调用方处理
            }
        }
    }
    return conn;
}

// 归还连接到池中，并唤醒一个等待中的取连接线程
// 参数 conn: 用完的连接指针
void ConnPool::returnConnection(MysqlClient *conn)
{
    if (conn == nullptr)//如果连接为空，直接返回
        return;
    pthread_mutex_lock(&m_mutex);
    m_connQueue.push(conn);//将连接放入空闲队列
    pthread_cond_signal(&m_cond); // 唤醒一个等待取连接的线程
    pthread_mutex_unlock(&m_mutex);
}

// 销毁池中所有连接（服务器退出时调用）
void ConnPool::destroy()
{
    pthread_mutex_lock(&m_mutex);
    m_isStop = true;
    pthread_cond_broadcast(&m_cond); // 唤醒所有阻塞的取连接线程
    // 逐个销毁空闲连接（析构函数中 mysql_close）
    while (!m_connQueue.empty())
    {
        MysqlClient *conn = m_connQueue.front();
        m_connQueue.pop();
        delete conn;
    }
    pthread_mutex_unlock(&m_mutex);
}

// 获取当前空闲连接数（监控/调试用）
int ConnPool::getIdleCount()
{
    pthread_mutex_lock(&m_mutex);
    int count = (int)m_connQueue.size();
    pthread_mutex_unlock(&m_mutex);
    return count;
}

/************************************************************
 *  ConSocket 类实现
 *  处理单个客户端连接的数据收发与业务逻辑分发
 ************************************************************/

// 重新注册 epoll 事件（EPOLLONESHOT 触发后必须重新激活）
void ConSocket::RestEvent()
{
    struct epoll_event ev;
    ev.data.ptr = this;
    ev.events = EPOLLIN | EPOLLONESHOT;
    //注册读事件和事件，EPOLLONESHOT 触发后必须重新激活，EPOLLIN 事件持续存在
    epoll_ctl(m_epfd, EPOLL_CTL_MOD, m_fd, &ev);
    // 重新注册 epoll 事件，确保客户端数据可读
}

// 解析客户端 JSON 请求，提取 type 字段存入 m_OpType
// 参数 buff: 客户端发来的原始字符串
void ConSocket::Get_OpType(char *buff)
{
    Json::Reader Read;
    m_val.clear();
    if (!Read.parse(buff, m_val))
    {
        m_OpType = -1; // 解析失败，标记为无效操作
        return;
    }
    m_OpType = m_val["type"].asInt();//提取操作类型字段
}

// 将 Json 对象序列化为单行字符串（去掉 FastWriter 可能带的尾部换行）
static string toJsonLine(const Json::Value &val)
{
    Json::FastWriter writer;
    string s = writer.write(val);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

// 向客户端发送 {"status":"OK"}（末尾追加 \n 作为消息边界）
void ConSocket::Send_Ok()
{
    Json::Value val;
    val["status"] = "OK";
    string json_str = toJsonLine(val) + "\n";
    send(m_fd, json_str.c_str(), json_str.length(), 0);
}

// 向客户端发送 {"status":"ERR"}（末尾追加 \n 作为消息边界）
void ConSocket::Send_Err()
{
    Json::Value val;
    val["status"] = "ERR";
    string json_str = toJsonLine(val) + "\n";
    send(m_fd, json_str.c_str(), json_str.length(), 0);
}

// 向客户端发送一个 Json 对象（末尾追加 \n 作为消息边界）
// 参数 root: 待发送的 Json::Value
void ConSocket::Send_JsonObj(Json::Value &root)
{
    string json_str = toJsonLine(root) + "\n";
    send(m_fd, json_str.c_str(), json_str.length(), 0);
}

// 处理用户注册请求：从 m_val 取参数，调用 Db_User_Register
void ConSocket::User_Register()
{
    string username = m_val["user_name"].asString();
    string usertel = m_val["user_tel"].asString();
    string passwd = m_val["user_passwd"].asString();
    ConnGuard cli; // 从连接池获取连接，析构时自动归还
    if (!cli.valid())
    {
        Send_Err();
        return;
    }
    if (!cli->Db_User_Register(usertel, username, passwd))
    {
        Send_Err();
        return;
    }
    Send_Ok();
}

// 处理用户登录请求：验证手机号密码，返回用户名
void ConSocket::User_Login()
{
    string usertel = m_val["user_tel"].asString();
    string passwd = m_val["user_passwd"].asString();
    string username;
    ConnGuard cli; // 从连接池获取连接，析构时自动归还
    if (!cli.valid())//如果连接为空，返回 ERR
    {
        Send_Err();
        return;
    }
    if (!cli->Db_User_Login(usertel, username, passwd))//验证手机号密码
    {
        Send_Err();
        return;
    }
    Json::Value res;//创建 JSON 对象
    res["status"] = "OK";//设置状态为 OK
    res["user_name"] = username;//设置用户名
    Send_JsonObj(res);//发送 JSON 对象
}

// 处理查看所有票务请求：查询并返回票务列表
void ConSocket::Show_Ticket()
{
    Json::Value res;//创建 JSON 对象
    ConnGuard cli; // 从连接池获取连接，析构时自动归还
    if (!cli.valid())//如果连接为空，返回 ERR
    {
        Send_Err();
        return;
    }
    if (!cli->Db_Show_Ticket(res))//查询所有票务
    {
        Send_Err();
        return;
    }
    Send_JsonObj(res);
}

// 处理预定票务请求：调用 Db_Yd_Ticket
void ConSocket::Yd_Ticket()
{
    string user_tel = m_val["user_tel"].asString();
    string tk_id = m_val["ticket_id"].asString();
    ConnGuard cli;
    if (!cli.valid())
    {
        Send_Err();
        return;
    }
    if (!cli->Db_Yd_Ticket(user_tel, tk_id))
    {
        Send_Err();
        return;
    }
    Send_Ok();
}

// 处理查看我的预约请求：查询指定用户的有效预约
void ConSocket::Show_My_Ticket()
{
    string usertel = m_val["user_tel"].asString();
    Json::Value res;
    ConnGuard cli;
    if (!cli.valid())
    {
        Send_Err();
        return;
    }
    if (!cli->Db_Show_My_Ticket(usertel, res))
    {
        Send_Err();
        return;
    }
    Send_JsonObj(res);
}

// 处理取消预约请求：调用 Db_Cancel_Ticket
void ConSocket::Cancel_Ticket()
{
    string usertel = m_val["user_tel"].asString();
    string tk_id = m_val["ticket_id"].asString();
    ConnGuard cli;
    if (!cli.valid())
    {
        Send_Err();
        return;
    }
    if (!cli->Db_Cancel_Ticket(usertel, tk_id))
    {
        Send_Err();
        return;
    }
    Send_Ok();
}

// 主处理函数：接收客户端数据 → 按 \n 分包 → 解析操作类型 → 分发到对应业务函数
// 使用 m_recvBuf 累积跨多次 recv 的数据，正确处理拆包与粘包
void ConSocket::Handle_Data()
{
    char buff[1024]{0};
    int n = recv(m_fd, buff, 1023, 0);
    if (n <= 0)
    {
        g_alive--;     // 连接关闭或出错，当前存活连接数 -1
        delete this;   // 连接关闭或出错，释放对象
        return;
    }
    // 将本次收到的数据追加到接收缓冲区
    m_recvBuf.append(buff, n);

    // 循环处理缓冲区中所有以 \n 结尾的完整消息
    size_t pos;
    while ((pos = m_recvBuf.find('\n')) != string::npos)
    {
        string msg = m_recvBuf.substr(0, pos); // 提取一条完整消息（不含 \n）
        m_recvBuf.erase(0, pos + 1);           // 从缓冲区移除已处理消息

        Get_OpType(&msg[0]);
        // 根据操作类型分发
        switch (m_OpType)
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
        default:
            Send_Err();
            break;
        }
    }
    RestEvent(); // 重新激活 epoll 事件，准备接收下一次数据
}

/************************************************************
 *  TcpSer 类实现
 *  TCP 服务器主控：socket + epoll + 线程池的组装与事件循环
 ************************************************************/

// 服务器初始化：创建监听 socket 并创建 epoll 句柄
// 返回值: true 成功；false 失败
bool TcpSer::Ser_Init()
{
    if (!create_socket())// 创建并绑定套接字
        return false;
    m_epfd = epoll_create1(0);  // 创建 epoll 句柄
    if (m_epfd == -1)
        return false;
    return true;
}

// 启动服务器：初始化 → 加入监听 → 启动线程池 → 进入 epoll 事件循环
void TcpSer::Run()
{
    if (!Ser_Init())
    {
        cout << "服务初始化失败" << endl;
        return;
    }
    // 创建监听 socket 对象并加入 epoll
    LisSocket *sock = new LisSocket(m_sockfd, m_epfd);
    struct epoll_event ev;
    ev.data.ptr = sock;
    ev.events = EPOLLIN | EPOLLONESHOT; // 监听读事件，设置为单次触发
    epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_sockfd, &ev);
     // 加入监听 socket 到 epoll
    // 初始化并启动线程池
    m_pool.Thread_Pool_Init();
    m_pool.Start_Thread();
    cout << "服务器启动成功，监听端口：" << m_conf.port << endl;

    // 主事件循环
    while (true)
    {
        m_num = epoll_wait(m_epfd, m_evs, EV_MAX, 5000);
        if (m_num > 0)
            do_events();
    }
}

// 创建监听 socket：设置端口复用 → 绑定 → 监听
// 返回值: true 成功；false 失败
bool TcpSer::create_socket()
{
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd == -1)
    {
        perror("socket create err");
        return false;
    }
    int opt = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // 地址复用
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)); // 端口复用

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(m_conf.port);
    saddr.sin_addr.s_addr = inet_addr(m_conf.ips.c_str());

    if (bind(m_sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    {
        perror("bind err");
        return false;
    }
    if (listen(m_sockfd, m_conf.lismax) < 0)
    {
        perror("listen err");
        return false;
    }
    // 将监听 socket 设为非阻塞，避免 LisSocket::Handle_Data 中循环 accept 时
    // 第二次 accept 因无新连接而阻塞，导致事件线程卡死
    int flags = fcntl(m_sockfd, F_GETFL, 0);
    fcntl(m_sockfd, F_SETFL, flags | O_NONBLOCK);
    return true;
}

// 处理 epoll 返回的就绪事件
// 高并发改造：监听 socket（LisSocket）在事件线程内直接循环 accept，不进入业务线程池，
// 避免建连高峰被 4 个 worker/1024 任务队列阻塞或丢弃；普通连接仍投递到线程池处理业务
void TcpSer::do_events()
{
    for (int i = 0; i < m_num; i++)
    {
        if (m_evs[i].events & EPOLLIN)
        {
            Socket *ptr = (Socket *)m_evs[i].data.ptr;
            if (dynamic_cast<LisSocket *>(ptr) != nullptr)
                ptr->Handle_Data();      // 监听：事件线程内取空 backlog
            else
                m_pool.Add_Task(ptr);    // 业务连接：投递到线程池
        }
    }
}

/************************************************************
 *  主函数
 *  读取配置 → 初始化连接池 → 创建并运行服务器
 ************************************************************/
int main()
{
    // 1. 读取服务器配置
    SerConf conf;
    conf.ReadConf("my.conf");
    conf.PrintInfo();

    // 2. 初始化数据库连接池（必须在服务器启动前完成）
    if (!ConnPool::getInstance()->init(conf.db_ip, conf.db_port, conf.db_name,
                                       conf.db_username, conf.db_passwd, conf.pool_size))
    {
        cout << "数据库连接池初始化失败，服务退出" << endl;
        return -1;
    }

    // 3. 创建服务器并运行
    TcpSer ser(conf);
    ser.Run();

    // 4. 服务器退出时销毁连接池
    ConnPool::getInstance()->destroy();
    return 0;
}
