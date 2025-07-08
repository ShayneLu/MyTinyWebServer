#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>

#include "./http/http_conn.h"         // HTTP连接处理类
#include "./threadpool/threadpool.h"  // 线程池实现
#include "./log/log.h"  // 显式声明对Log类的依赖

// 全局常量定义
const int MAX_FD = 65536;            // 最大文件描述符数量（限制并发连接数）
const int MAX_EVENT_NUMBER = 10000;  // epoll最大监听事件数
const int TIMESLOT = 5;              // 定时器最小超时单位（秒）

class WebServer {
   public:
    WebServer();   // 构造函数
    ~WebServer();  // 析构函数（释放资源）

    // 初始化服务器配置
    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    // 核心功能模块初始化
    void thread_pool();  // 初始化线程池
    void sql_pool();     // 初始化数据库连接池
    void log_write();    // 初始化日志系统
    void trig_mode();    // 设置触发模式（LT/ET）
    void eventListen();  // 启动监听socket
    void eventLoop();    // 主事件循环

    // 定时器管理
    void timer(int connfd, struct sockaddr_in client_address);  // 创建定时器
    void adjust_timer(util_timer *timer);                       // 调整定时器
    void deal_timer(util_timer *timer, int sockfd);  // 处理超时定时器

    // 事件处理
    bool dealclientdata();                                  // 处理新客户端连接
    bool dealwithsignal(bool &timeout, bool &stop_server);  // 处理信号
    void dealwithread(int sockfd);                          // 处理读事件
    void dealwithwrite(int sockfd);                         // 处理写事件

   public:
    // ---------- 基础配置 ----------
    int m_port;        // 服务器监听端口
    char *m_root;      // 服务器根目录路径
    int m_log_write;   // 日志写入方式（0同步/1异步）
    int m_close_log;   // 是否关闭日志（0不关闭/1关闭）
    int m_actormodel;  // 并发模型（0 Proactor/1 Reactor）

    // ---------- 网络相关 ----------
    int m_pipefd[2];   // 管道（用于统一事件源，处理信号）
    int m_epollfd;     // epoll实例的文件描述符
    http_conn *users;  // HTTP连接对象数组（每个客户端对应一个）

    // ---------- 数据库相关 ----------
    connection_pool *m_connPool;  // 数据库连接池指针
    string m_user;                // 数据库用户名
    string m_passWord;            // 数据库密码
    string m_databaseName;        // 数据库名
    int m_sql_num;                // 数据库连接池大小

    // ---------- 线程池相关 ----------
    threadpool<http_conn> *m_pool;  // 线程池指针
    int m_thread_num;               // 线程池线程数量

    // ---------- epoll事件相关 ----------
    epoll_event events[MAX_EVENT_NUMBER];  // 存储epoll返回的事件

    // ---------- socket相关 ----------
    int m_listenfd;        // 监听socket的文件描述符
    int m_OPT_LINGER;      // 是否优雅关闭连接（0否/1是）
    int m_TRIGMode;        // 触发组合模式（0~3）
    int m_LISTENTrigmode;  // listenfd触发模式（0 LT/1 ET）
    int m_CONNTrigmode;    // connfd触发模式（0 LT/1 ET）

    // ---------- 定时器相关 ----------
    client_data *users_timer;  // 客户端定时器数据数组
    Utils utils;               // 工具类（处理信号、定时器等）
};

#endif
