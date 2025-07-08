#ifndef LST_TIMER
#define LST_TIMER

// 系统头文件包含
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../log/log.h"

// 前向声明定时器类
class util_timer;

// 用户数据结构：
// 保存客户端socket地址、文件描述符和定时器
struct client_data {
    sockaddr_in address;  // 客户端socket地址
    int sockfd;           // 客户端文件描述符
    util_timer *timer;    // 指向对应的定时器
};

// 定时器类
class util_timer {
   public:
    util_timer() : prev(NULL), next(NULL) {}  // 构造函数，初始化前后指针为空

   public:
    time_t expire;  // 超时时间，使用绝对时间

    // 回调函数，用于超时处理，接收一个client_data指针作为参数
    void (*cb_func)(client_data *);

    client_data *user_data;  // 用户数据，指向client_data结构体
    util_timer *prev;        // 指向前一个定时器
    util_timer *next;        // 指向后一个定时器
};

// 定时器链表，升序、双向链表
class sort_timer_lst {
   public:
    sort_timer_lst();   // 构造函数
    ~sort_timer_lst();  // 析构函数，销毁所有定时器

    // 添加定时器，内部调用私有成员add_timer
    void add_timer(util_timer *timer);

    // 调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer);

    // 删除定时器
    void del_timer(util_timer *timer);

    // 定时任务处理函数
    // 遍历链表，处理到期的定时器，并删除它们
    void tick();

   private:
    // 私有成员，被公有成员add_timer和adjust_timer调用
    // 主要用于调整链表内部结点，使其符合升序链表要求
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;  // 头结点
    util_timer *tail;  // 尾结点
};

// 工具类，提供通用的工具函数
class Utils {
   public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);  // 初始化时间间隔

    // 设置文件描述符非阻塞
    int setnonblocking(int fd);

    // 将文件描述符注册到epoll事件表中
    // epollfd: epoll文件描述符
    // fd: 要注册的文件描述符
    // one_shot: 是否开启EPOLLONESHOT
    // TRIGMode: 触发模式（ET或LT）
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    // sig: 信号
    // handler: 信号处理函数
    // restart: 是否自动重启被中断的系统调用
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务
    // 重新定时以不断触发SIGALRM信号
    void timer_handler();

    // 显示错误信息
    void show_error(int connfd, const char *info);

   public:
    static int *u_pipefd;        // 管道文件描述符
    sort_timer_lst m_timer_lst;  // 定时器链表
    static int u_epollfd;        // epoll文件描述符
    int m_TIMESLOT;              // 时间间隔
};

// 定时器回调函数
// 从内核事件表删除事件，关闭文件描述符，释放连接资源
void cb_func(client_data *user_data);

#endif
