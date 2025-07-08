/*************************************************************
 * 基于循环数组实现的线程安全阻塞队列
 * 特性：
 *   - 线程安全：所有操作通过互斥锁（m_mutex）和条件变量（m_cond）保护
 *   -
 *阻塞行为：当队列空时，pop操作会阻塞；队列满时，push操作可配置为阻塞或返回失败
 *   - 环形缓冲区：通过取模运算实现高效循环利用（m_back = (m_back + 1) % m_max_size）
**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>

#include <iostream>

#include "../lock/locker.h"  // 自定义的锁和条件变量封装
using namespace std;

template <class T>  // 模板类，支持任意数据类型
class block_queue {
   public:
    // 构造函数：初始化队列容量
    explicit block_queue(int max_size = 1000) {
        if (max_size <= 0) exit(-1);  // 非法容量检查

        m_max_size = max_size;
        //以下4个成员变量，用于管理循环数组，都是共享资源，操作时需加锁
        m_array = new T[max_size];  // 动态分配数组。明明是队列，但是没有用queue，而是用数组来实现
        m_size = 0;                 // 当前元素数
        m_front = -1;               // 队首下标（初始无效）
        m_back = -1;                // 队尾下标（初始无效）
    }

    // 清空队列（不释放内存）
    void clear() {
        m_mutex.lock();  // 加锁
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();  // 解锁
    }

    // 析构函数：释放动态数组
    ~block_queue() {
        m_mutex.lock();
        if (m_array != NULL) delete[] m_array;  // 释放内存
        m_mutex.unlock();
    }

    // 判断队列是否满
    bool full() {
        m_mutex.lock();
        bool ret = (m_size >= m_max_size);  // 当前大小 >= 容量
        m_mutex.unlock();
        return ret;
    }

    // 判断队列是否空
    bool empty() {
        m_mutex.lock();
        bool ret = (m_size == 0);  // 当前大小为0
        m_mutex.unlock();
        return ret;
    }

    // 获取队首元素（非阻塞）
    bool front(T &value) {
        m_mutex.lock();
        if (m_size == 0) {  // 空队列检查
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];  // 取出队首
        m_mutex.unlock();
        return true;
    }

    // 获取队尾元素（非阻塞）
    bool back(T &value) {
        m_mutex.lock();
        if (m_size == 0) {  // 空队列检查
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];  // 取出队尾
        m_mutex.unlock();
        return true;
    }

    // 返回当前队列元素数量
    int size() {
        m_mutex.lock();
        int ret = m_size;
        m_mutex.unlock();
        return ret;
    }

    // 返回队列最大容量
    int max_size() {
        m_mutex.lock();
        int ret = m_max_size;
        m_mutex.unlock();
        return ret;
    }

    // 向队列尾部添加元素（生产者调用）
    bool push(const T &item) {
        m_mutex.lock();
        if (m_size >= m_max_size) {  // 队列已满
            m_cond.broadcast();      // 唤醒可能阻塞的消费者
            m_mutex.unlock();
            return false;  // 返回失败（可改为阻塞等待）
        }

        // 循环数组操作
        // 比如最大容量1000，0~999，当前999，+1=1000，取模后为0，即m_back=0，将日志行推入此处
        m_back = (m_back + 1) % m_max_size;  // 队尾指针后移一个单位，如果超过最大容量，则取模
        m_array[m_back] = item;              // 存入元素
        m_size++;

        m_cond.broadcast();  // 唤醒等待的消费者
        m_mutex.unlock();
        return true;
    }

    // 从队列头部取出元素（消费者调用，阻塞模式）
    bool pop(T &item) {
        m_mutex.lock();
        while (m_size <= 0) {                   // 队列空时等待
            if (!m_cond.wait(m_mutex.get())) {  // 条件变量等待（自动释放锁）
                m_mutex.unlock();               // 被唤醒后重新加锁
                return false;                   // 等待失败（如线程被中断）
            }
        }

        // 取出队首元素
        m_front = (m_front + 1) % m_max_size;  // 队首指针后移
        item = m_array[m_front];    //item就是取出的队首元素
        m_size--;
        m_mutex.unlock();
        return true;
    }

    // 带超时的pop操作（单位：毫秒）
    bool pop(T &item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);  // 获取当前时间

        m_mutex.lock();
        if (m_size <= 0) {
            // 计算绝对超时时间
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t)) {  // 超时等待
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0) {  // 双重检查（避免虚假唤醒）
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

   private:
    locker m_mutex;  // 互斥锁（封装pthread_mutex_t）
    cond m_cond;     // 条件变量（封装pthread_cond_t）

    T *m_array;      // 循环数组指针
    int m_size;      // 当前元素数量
    int m_max_size;  // 队列最大容量
    int m_front;     // 队首下标（指向最早入队的元素）
    int m_back;      // 队尾下标（指向最新入队的元素）
};

#endif
