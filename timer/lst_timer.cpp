#include "../http/http_conn.h"
#include "lst_timer.h"

sort_timer_lst::sort_timer_lst() {
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst() {
    util_timer *tmp = head;
    while (tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer) {
    if (!timer) {
        return; //如果节点(即timer)为空，即不需要往链表中添加定时器，直接返回
    }
    if (!head) {//如果链表为空，直接插入，将timer作为头节点和尾节点
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) {//如果timer的到期时间小于头节点的到期时间，则将timer插入到头节点之前
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);//否则，调用调用add_timer()方法插入到合适位置
}

void sort_timer_lst::adjust_timer(util_timer *timer) {
    if (!timer) {   //如果timer为空，没必要调整，直接返回
        return;
    }
    util_timer *tmp = timer->next; 
    if (!tmp || (timer->expire < tmp->expire)) {// 如果定时器的超时时间仍然小于下一个节点，无需调整
        return;
    }
    if (timer == head) {// 如果定时器是头节点，将其从链表中移除，调用add_timer()方法并重新插入
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    } else {// 如果定时器不是头节点，将其从链表中移除，调用add_timer()方法并重新插入
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer *timer) {
    if (!timer) {
        return;
    }
    if ((timer == head) && (timer == tail)) {  // 如果链表只有一个节点
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head) {  // 如果删除的是头节点
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail) {  // 如果删除的是尾节点
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;  // 如果删除的是中间节点
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::tick() {
    if (!head) {
        return;
    }

    time_t cur = time(NULL);    // 获取当前时间
    util_timer *tmp = head;    
    while (tmp) {// 遍历定时器链表，找到所有超时的定时器
        if (cur < tmp->expire) {// 如果当前时间小于定时器的超时时间，则跳出循环
            break;
        }
        tmp->cb_func(tmp->user_data);// 如果超时，调用回调函数，处理超时事件
        head = tmp->next;   //当前tmp超时，下面5行代码都是为了删除当前tmp定时器
        if (head) {
            head->prev = NULL;
        }
        delete tmp;  // 删除超时的定时器
        tmp = head;  // 指向下一个定时器，继续检测
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) {
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;//遍历方式怪怪的，不是从head开始，而是从head的下一个节点开始。是∵既然传入了head，那么head就一定存在了。
    while (tmp) {   // 遍历链表，找到第一个超时时间大于新定时器的节点tmp
        if (timer->expire < tmp->expire) {//将timer插入到tmp之前
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp; // 更新prev为tmp，继续遍历
        tmp = tmp->next; // 更新tmp为tmp的下一个节点，继续遍历
    }
    if (!tmp) { // 如果遍历到链表尾部仍未找到，将新定时器插入到尾部
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

void Utils::init(int timeslot) { m_TIMESLOT = timeslot; }   //timeslot，时间间隔，单位是秒

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);  // 获取文件描述符的当前状态
    int new_option = old_option | O_NONBLOCK;  // 设置非阻塞模式
    fcntl(fd, F_SETFL, new_option);  // 设置文件描述符的非阻塞模式
    return old_option;  // 返回文件描述符的当前状态
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数
void Utils::sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);// 通过管道向主线程发送信号
    errno = save_errno;
}

// 设置信号函数，将信号处理函数设置为sig_handler()，注意只是设置函数但不会主动发送信号。
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;    //是 POSIX 标准的一部分，里面函数指针sa_handler，可以指向sig_handler()
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;  // 设置信号处理函数为sig_handler()
    if (restart) sa.sa_flags |= SA_RESTART; // 如果 restart 为 true，设置 SA_RESTART 标志
    sigfillset(&sa.sa_mask);    // 设置信号掩码，阻塞所有信号
    assert(sigaction(sig, &sa, NULL) != -1);     // 调用 sigaction 系统调用，设置信号处理行为
}

/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：
// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

// 以下是改进：在处理定时器前，临时屏蔽SIGALRM 信号，确保 tick()方法不会被中断。 
// void Utils::timer_handler() {
//     sigset_t mask;  // 定义信号掩码
//     sigemptyset(&mask);  // 初始化信号掩码为空
//     sigaddset(&mask, SIGALRM);  // 将 SIGALRM 信号添加到信号掩码中

//     // 屏蔽 SIGALRM 信号，确保 tick() 方法不会被中断
//     sigprocmask(SIG_BLOCK, &mask, NULL);

//     m_timer_lst.tick();  // 调用 tick() 方法，处理所有到期的定时器
//     alarm(m_TIMESLOT);  // 重新设置定时器，实现循环检查

//     // 解除对 SIGALRM 信号的屏蔽，允许后续的 SIGALRM 信号被处理
//     sigprocmask(SIG_UNBLOCK, &mask, NULL);
// }
/*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/


void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}


int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data) {
    // 从epoll事件表中删除客户端的socket
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);   // 关闭客户端的socket
    http_conn::m_user_count--;  // 减少HTTP连接的计数
}
