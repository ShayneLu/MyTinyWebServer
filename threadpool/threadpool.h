#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>  // 包含POSIX线程库，用于线程操作

#include <cstdio>     // 包含C标准输入输出库，提供printf等函数
#include <exception>  // 包含C++标准异常处理库
#include <list>  // 包含C++标准库的list容器，用于实现请求队列

#include "../CGImysql/sql_connection_pool.h"  // 包含数据库连接池的头文件
#include "../lock/locker.h"  // 包含自定义的互斥锁和信号量封装

/**
 * @brief 线程池类，模板参数T是任务类型
 * @tparam T 任务类型，通常是HTTP连接类或CGI请求类
 */
template <typename T>
class threadpool {
   public:
    /**
     * @brief 线程池构造函数
     * @param actor_model 模型的切换，0表示Proactor模式，1表示Reactor模式
     * @param connPool 数据库连接池指针
     * @param thread_number 线程池中线程的数量，默认为8
     * @param max_requests
     * 请求队列中最多允许的、等待处理的请求的数量，默认为10000
     */
    threadpool(int actor_model, connection_pool *connPool,
               int thread_number = 8, int max_request = 10000);

    /**
     * @brief 线程池析构函数
     */
    ~threadpool();

    /**
     * @brief 向请求队列中添加任务（Proactor模式下使用）
     * @param request 指向任务对象的指针
     * @param state 任务的状态，例如读或写
     * @return true 添加成功
     * @return false 添加失败（队列已满）
     */
    bool append(T *request, int state);

    /**
     * @brief 向请求队列中添加任务（Reactor模式下使用）
     * @param request 指向任务对象的指针
     * @return true 添加成功
     * @return false 添加失败（队列已满）
     */
    bool append_p(T *request);

   private:
    /**
     * @brief 工作线程运行的函数，作为pthread_create的入口函数
     *        它不断从工作队列中取出任务并执行之
     * @param arg 线程参数，实际上是threadpool对象的this指针
     * @return void* 返回值，通常为NULL
     */
    static void *worker(void *arg);

    /**
     * @brief 线程池中每个工作线程的实际运行逻辑
     *        从请求队列中取出任务并调用其process方法进行处理
     */
    void run();

   private:
    int m_thread_number;  // 线程池中的线程数
    int m_max_requests;   // 请求队列中允许的最大请求数
    pthread_t *
        m_threads;  // 描述线程池的数组，存储所有工作线程的线程ID，其大小为m_thread_number
    std::list<T *> m_workqueue;  // 请求队列，存储待处理的任务对象指针
    locker m_queuelocker;  // 保护请求队列的互斥锁，确保线程安全
    sem m_queuestat;  // 信号量，用于判断是否有任务需要处理，当有任务时信号量值增加
    connection_pool *m_connPool;  // 数据库连接池指针，用于数据库操作
    int m_actor_model;  // 模型切换标志，0表示Proactor模式，1表示Reactor模式
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool,
                          int thread_number, int max_requests)
    : m_actor_model(actor_model),
      m_thread_number(thread_number),
      m_max_requests(max_requests),
      m_threads(NULL),
      m_connPool(connPool) {
    // 检查线程数和最大请求数是否合法
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();  // 抛出异常表示参数错误

    // 创建线程ID数组
    // pthread_t 是POSIX线程库中定义的数据类型，用于存储线程的标识符（线程ID）
    // 每个pthread_t变量可以存储一个线程的ID，通过这个ID可以引用和控制对应的线程
    // 这里创建一个pthread_t数组，用于存储所有工作线程的ID，数组大小为m_thread_number
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) throw std::exception();  // 抛出异常表示内存分配失败

    // 创建并启动所有工作线程
    for (int i = 0; i < thread_number; ++i) {
        // 创建线程，worker是线程的入口函数，this作为参数传递给worker
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;      // 创建失败则释放已分配的内存
            throw std::exception();  // 抛出异常
        }
        // 将线程设置为分离状态，线程结束后自动释放资源
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;      // 分离失败则释放已分配的内存
            throw std::exception();  // 抛出异常
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;  // 释放线程ID数组的内存
}

template <typename T>
bool threadpool<T>::append(T *request, int state) {
    m_queuelocker.lock();  // 加锁保护请求队列
    // 判断请求队列是否已满
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();  // 解锁
        return false;            // 队列已满，添加失败
    }
    request->m_state = state;        // 设置任务的状态
    m_workqueue.push_back(request);  // 将任务添加到队列尾部
    m_queuelocker.unlock();          // 解锁
    m_queuestat.post();  // 信号量加一，表示有新任务到来
    return true;         // 添加成功
}

template <typename T>
bool threadpool<T>::append_p(T *request) {
    m_queuelocker.lock();  // 加锁保护请求队列
    // 判断请求队列是否已满
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();  // 解锁
        return false;            // 队列已满，添加失败
    }
    m_workqueue.push_back(request);  // 将任务添加到队列尾部
    m_queuelocker.unlock();          // 解锁
    m_queuestat.post();  // 信号量加一，表示有新任务到来
    return true;         // 添加成功
}

template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg;  // 将参数转换为threadpool指针
    pool->run();  // 调用run方法执行线程的实际工作
    return pool;  // 返回线程池指针
}

template <typename T>
void threadpool<T>::run() {
    while (true)  // 循环，工作线程持续运行
    {
        m_queuestat.wait();  // 信号量减一，等待任务。如果队列为空，则线程阻塞
        m_queuelocker.lock();  // 加锁保护请求队列
        // 检查请求队列是否为空（在获取锁后再次检查，防止虚假唤醒）
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();  // 解锁
            continue;                // 继续等待下一个任务
        }
        T *request = m_workqueue.front();  // 从队列头部取出任务
        m_workqueue.pop_front();           // 移除队列头部的任务
        m_queuelocker.unlock();            // 解锁

        if (!request)  // 如果取出的任务为空，则继续
            continue;

        // 根据模型切换标志执行不同的处理逻辑
        if (1 == m_actor_model)  // Reactor模式
        {
            if (0 == request->m_state)  // 读操作
            {
                if (request->read_once())  // 读取数据
                {
                    request->improv = 1;  // 标记为需要改进（或已处理）
                    // 使用RAII机制管理数据库连接，确保连接的正确获取和释放
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();  // 处理请求
                } else                   // 读取失败
                {
                    request->improv = 1;  // 标记为需要改进
                    request->timer_flag = 1;  // 标记为需要关闭连接（或定时器处理）
                }
            } else  // 写操作
            {
                if (request->write())  // 写入数据
                {
                    request->improv = 1;  // 标记为需要改进
                } else                    // 写入失败
                {
                    request->improv = 1;      // 标记为需要改进
                    request->timer_flag = 1;  // 标记为需要关闭连接
                }
            }
        } else  // Proactor模式
        {
            // 使用RAII机制管理数据库连接
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();  // 直接处理请求
        }
    }
}
#endif

