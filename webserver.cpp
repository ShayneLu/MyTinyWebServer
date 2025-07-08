#include "./log/log.h"  // 显式声明对Log类的依赖
#include "webserver.h"

WebServer::WebServer() {
    // http_conn类对象
    users = new http_conn[MAX_FD];  // 使用new来创建动态的http_conn类对象数组，数组大小为MAX_FD

    // root文件夹路径
    char server_path[200];
    getcwd(server_path,
        200);                // server_path是当前工作目录，也即WebServer项目所在目录
    char root[6] = "/root";  // 字符串“/root”，表示根目录
    m_root = (char *)malloc(strlen(server_path) + strlen(root) +
                            1);  // 为m_root分配内存，大小为server_path和root的长度之和加1
    strcpy(m_root,
        server_path);      // 将server_path复制到m_root，也即WebServer/root目录
    strcat(m_root, root);  // 将root追加到m_root

    // 定时器
    users_timer = new client_data[MAX_FD];  // 为每个客户端连接都创建定时器
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
    int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode() {
    // LT + LT 模式：监听socket和连接socket都采用水平触发模式
    if (0 == m_TRIGMode) {
        m_LISTENTrigmode = 0;  // 监听socket使用水平触发(LT)
        m_CONNTrigmode = 0;    // 连接socket使用水平触发(LT)
    }
    // LT + ET 模式：监听socket采用水平触发，连接socket采用边缘触发
    else if (1 == m_TRIGMode) {
        m_LISTENTrigmode = 0;  // 监听socket使用水平触发(LT)
        m_CONNTrigmode = 1;    // 连接socket使用边缘触发(ET)
    }
    // ET + LT 模式：监听socket采用边缘触发，连接socket采用水平触发
    else if (2 == m_TRIGMode) {
        m_LISTENTrigmode = 1;  // 监听socket使用边缘触发(ET)
        m_CONNTrigmode = 0;    // 连接socket使用水平触发(LT)
    }
    // ET + ET 模式：监听socket和连接socket都采用边缘触发模式
    else if (3 == m_TRIGMode) {
        m_LISTENTrigmode = 1;  // 监听socket使用边缘触发(ET)
        m_CONNTrigmode = 1;    // 连接socket使用边缘触发(ET)
    }
}

// 日志系统
void WebServer::log_write() {
    if (0 == m_close_log)  // 是否启用日志（0启用/1关闭）
    {
        // 初始化日志
        if (1 == m_log_write)  // 日志写入方式（0同步/1异步）
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool() {
    // 创建数据库连接池的唯一实例并赋值给类WebServer的成员变量m_connPool，
    // connection_pool*类型
    m_connPool = connection_pool::GetInstance();
    // 初始化数据库连接池，配置参数。
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool() {
    // 线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen() {
    // 创建一个监听套接字，PF_INET表示IPv4协议，SOCK_STREAM表示TCP协议
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);  // 确保套接字创建成功

    // 设置套接字的优雅关闭选项
    if (0 == m_OPT_LINGER) {
        // 如果m_OPT_LINGER为0，表示立即关闭连接，不等待未发送的数据
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else if (1 == m_OPT_LINGER) {
        // 如果m_OPT_LINGER为1，表示优雅关闭连接，等待未发送的数据
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;                                  // 用于存储函数调用的返回值
    struct sockaddr_in address;                   // 定义服务器地址结构
    bzero(&address, sizeof(address));             // 清空地址结构
    address.sin_family = AF_INET;                 // 设置地址族为IPv4
    address.sin_addr.s_addr = htonl(INADDR_ANY);  // 设置IP地址为任意地址
    address.sin_port = htons(m_port);  // 设置端口号，m_port为服务器监听的端口

    // 设置套接字选项，允许地址重用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // 将套接字绑定到指定的地址和端口
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);  // 确保绑定成功

    // 开始监听连接请求，5表示等待连接队列的最大长度
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);  // 确保监听成功

    // 初始化工具类，设置定时器的时间间隔为TIMESLOT
    utils.init(TIMESLOT);

    // 创建epoll事件表，用于监听文件描述符的事件
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);  // 确保epoll创建成功

    // 将监听套接字添加到epoll事件表中，监听其读事件
    // 参数：
    // m_epollfd：epoll实例的文件描述符
    // m_listenfd：监听的文件描述符
    // false：是否只监听一次
    // m_LISTENTrigmode：listenfd触发模式（0 LT/1 ET））
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;  // 将epoll文件描述符传递给HTTP连接类

    // 创建一对UNIX域套接字，用于进程间通信
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);  // 确保套接字对创建成功

    // 设置管道写端为非阻塞模式
    utils.setnonblocking(m_pipefd[1]);
    // 将管道读端添加到epoll事件表中，监听其读事件
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 设置信号处理函数，忽略SIGPIPE信号（防止写操作导致进程终止）
    utils.addsig(SIGPIPE, SIG_IGN);

    // 设置SIGALRM信号的处理函数，用于定时器任务
    utils.addsig(SIGALRM, utils.sig_handler, false);

    // 设置SIGTERM信号的处理函数，用于处理终止信号
    utils.addsig(SIGTERM, utils.sig_handler, false);

    // 启动定时器，每隔TIMESLOT秒触发一次SIGALRM信号
    alarm(TIMESLOT);

    // 将管道的文件描述符和epoll文件描述符传递给工具类
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user,
        m_passWord, m_databaseName);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    // struct client_data {
    // sockaddr_in address;  // 客户端socket地址
    // int sockfd;           // 客户端文件描述符
    // util_timer *timer;    // 指向对应的定时器
    // };
    users_timer[connfd].address =
        client_address;  // 将客户端socket地址赋值给users_timer[connfd].address
    users_timer[connfd].sockfd = connfd;  // 将客户端文件描述符赋值给users_timer[connfd].sockfd
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];  // 将users_timer[connfd]的地址赋值给timer->user_data
    timer->cb_func = cb_func;                 // 将cb_func赋值给timer->cb_func
    time_t cur = time(NULL);                  // 获取当前时间
    timer->expire = cur + 3 * TIMESLOT;  // 将当前时间加上3个单位赋值给timer->expire
    users_timer[connfd].timer = timer;   // 将timer赋值给users_timer[connfd].timer
    utils.m_timer_lst.add_timer(timer);  // 将timer添加到链表中
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;  // 也即超时时间为3*TIMESLOT=15秒
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);  // 调用定时器的回调函数，处理超时事件
    if (timer) {
        utils.m_timer_lst.del_timer(timer);  // 如果定时器存在，从定时器链表中删除该定时器
    }

    LOG_INFO("close fd %d",
        users_timer[sockfd].sockfd);  // 记录日志，关闭文件描述符
}

// 根据服务器的监听模式（水平触发 LT 或边缘触发
// ET）来接受客户端连接，并为每个新连接创建定时器。
bool WebServer::dealclientdata() {
    struct sockaddr_in client_address;                     // 用于存储客户端的地址信息
    socklen_t client_addrlength = sizeof(client_address);  // 客户端地址结构体的大小

    if (0 == m_LISTENTrigmode) {  // 如果监听模式为水平触发（LT）
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address,
            &client_addrlength);  // 接受客户端连接，返回连接的文件描述符
        if (connfd < 0) {         // 如果接受连接失败
            LOG_ERROR("%s:errno is:%d", "accept error", errno);  // 记录错误日志
            return false;  // 返回 false，表示处理失败
        }
        if (http_conn::m_user_count >= MAX_FD) {  // 如果当前连接数已达到最大连接数
            utils.show_error(connfd,
                "Internal server busy");  // 向客户端发送服务器繁忙的错误信息
            LOG_ERROR("%s", "Internal server busy");  // 记录错误日志
            return false;                             // 返回 false，表示处理失败
        }
        timer(connfd, client_address);  // 为新的连接创建定时器
    } else {                            // 如果监听模式为边缘触发（ET）
        while (1) {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address,
                &client_addrlength);  // 接受客户端连接，返回连接的文件描述符
            if (connfd < 0) {         // 如果接受连接失败
                LOG_ERROR("%s:errno is:%d", "accept error",
                    errno);  // 记录错误日志
                break;       // 跳出循环
            }
            if (http_conn::m_user_count >= MAX_FD) {  // 如果当前连接数已达到最大连接数
                utils.show_error(connfd,
                    "Internal server busy");  // 向客户端发送服务器繁忙的错误信息
                LOG_ERROR("%s", "Internal server busy");  // 记录错误日志
                break;                                    // 跳出循环
            }
            timer(connfd, client_address);  // 为新的连接创建定时器
        }
        return false;  // 返回 false，表示处理失败
    }
    return true;  // 返回 true，表示处理成功
}

// 处理信号事件，包括定时器超时和终止信号。
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server) {
    int ret = 0;         // 用于存储接收信号的返回值
    int sig;             // 用于存储接收到的信号
    char signals[1024];  // 用于存储接收到的信号数据

    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);  // 从管道中接收信号数据
    if (ret == -1) {                                       // 如果接收失败
        return false;                                      // 返回 false，表示处理失败
    } else if (ret == 0) {                                 // 如果接收到的数据长度为 0
        return false;                                      // 返回 false，表示处理失败
    } else {                                               // 如果成功接收到信号数据
        for (int i = 0; i < ret; ++i) {                    // 遍历接收到的信号数据
            switch (signals[i]) {                          // 根据信号类型进行处理
                case SIGALRM: {                            // 如果是 SIGALRM 信号
                    timeout = true;                        // 设置 timeout 标志为 true
                    break;                                 // 跳出 switch
                }
                case SIGTERM: {          // 如果是 SIGTERM 信号
                    stop_server = true;  // 设置 stop_server 标志为 true
                    break;               // 跳出 switch
                }
            }
        }
    }
    return true;  // 返回 true，表示处理成功
}

void WebServer::dealwithread(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;  // 获取与 sockfd 对应的定时器

    // reactor 模式
    if (1 == m_actormodel) {      // 如果当前是 reactor 模式
        if (timer) {              // 如果定时器存在
            adjust_timer(timer);  // 调整定时器的时间
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);  // 将读事件添加到线程池的任务队列中

        while (true) {                                // 循环等待事件处理完成
            if (1 == users[sockfd].improv) {          // 如果事件处理完成
                if (1 == users[sockfd].timer_flag) {  // 如果定时器标志为 1
                    deal_timer(timer, sockfd);        // 处理定时器事件
                    users[sockfd].timer_flag = 0;     // 重置定时器标志
                }
                users[sockfd].improv = 0;  // 重置事件处理标志
                break;                     // 跳出循环
            }
        }
    } else {  // 如果当前是 proactor 模式
        // proactor
        if (users[sockfd].read_once()) {  // 如果成功读取数据
            LOG_INFO("deal with the client(%s)",
                inet_ntoa(users[sockfd].get_address()->sin_addr));  // 记录日志，处理客户端数据

            // 若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);  // 将读事件添加到线程池的任务队列中

            if (timer) {              // 如果定时器存在
                adjust_timer(timer);  // 调整定时器的时间
            }
        } else {                        // 如果读取数据失败
            deal_timer(timer, sockfd);  // 处理定时器事件
        }
    }
}

void WebServer::dealwithwrite(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;  // 获取与 sockfd 对应的定时器

    // reactor 模式
    if (1 == m_actormodel) {      // 如果当前是 reactor 模式
        if (timer) {              // 如果定时器存在
            adjust_timer(timer);  // 调整定时器的时间
        }

        m_pool->append(users + sockfd, 1);  // 将写事件添加到线程池的任务队列中

        while (true) {                                // 循环等待事件处理完成
            if (1 == users[sockfd].improv) {          // 如果事件处理完成
                if (1 == users[sockfd].timer_flag) {  // 如果定时器标志为 1
                    deal_timer(timer, sockfd);        // 处理定时器事件
                    users[sockfd].timer_flag = 0;     // 重置定时器标志
                }
                users[sockfd].improv = 0;  // 重置事件处理标志
                break;                     // 跳出循环
            }
        }
    } else {  // 如果当前是 proactor 模式
        // proactor
        if (users[sockfd].write()) {  // 如果成功写入数据
            LOG_INFO("send data to the client(%s)",
                inet_ntoa(users[sockfd].get_address()->sin_addr));  // 记录日志，发送数据给客户端

            if (timer) {              // 如果定时器存在
                adjust_timer(timer);  // 调整定时器的时间
            }
        } else {                        // 如果写入数据失败
            deal_timer(timer, sockfd);  // 处理定时器事件
        }
    }
}

void WebServer::eventLoop() {
    bool timeout = false;      // 用于标记是否超时
    bool stop_server = false;  // 用于标记是否停止服务器

    while (!stop_server) {  // 主循环，直到服务器停止
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);  // 等待事件发生
        if (number < 0 && errno != EINTR) {    // 如果 epoll_wait 失败且不是因为中断
            LOG_ERROR("%s", "epoll failure");  // 记录错误日志
            break;                             // 跳出循环
        }

        for (int i = 0; i < number; i++) {   // 遍历所有发生的事件
            int sockfd = events[i].data.fd;  // 获取事件对应的文件描述符

            // 处理新到的客户连接
            if (sockfd == m_listenfd) {        // 如果是监听 socket 的事件
                bool flag = dealclientdata();  // 处理客户端连接
                if (false == flag) continue;   // 如果处理失败，继续下一个事件
            } else if (events[i].events &
                       (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {  // 如果发生错误或连接关闭
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;  // 获取与 sockfd 对应的定时器
                deal_timer(timer, sockfd);                      // 处理定时器事件
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {  // 如果是管道读事件
                bool flag = dealwithsignal(timeout, stop_server);                // 处理信号
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");  // 如果处理失败，记录错误日志
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN) {     // 如果是读事件
                dealwithread(sockfd);                  // 处理读事件
            } else if (events[i].events & EPOLLOUT) {  // 如果是写事件
                dealwithwrite(sockfd);                 // 处理写事件
            }
        }
        if (timeout) {                     // 如果超时
            utils.timer_handler();         // 处理定时器事件
            LOG_INFO("%s", "timer tick");  // 记录日志，定时器触发
            timeout = false;               // 重置超时标志
        }
    }
}
