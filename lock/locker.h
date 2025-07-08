#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>

#include <exception>


// 这个程序（locker.h）封装了三种线程同步机制，用于多线程编程中的并发控制：
// 信号量（sem）：控制同时访问某个资源的线程数量（如线程池的任务队列）。常用于生产者-消费者模型，限制缓冲区大小。
// 互斥锁（locker）：确保同一时间只有一个线程能访问共享数据，避免数据竞争（Data Race）。
// 条件变量（cond）：用于线程间的通信，条件不满足时进入等待，直到其他线程通知它条件已满足才被唤醒。



// 信号量类
class sem {
   public:
    // 构造函数，初始化信号量，初始值为0
    sem() {
        // sem_init参数：
        // 1. 指向信号量对象的指针
        // 2. pshared参数：0表示线程间共享，非0表示进程间共享
        // 3. 信号量的初始值
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();  // 初始化失败抛出异常
        }
    }

    // 带参数的构造函数，可以指定信号量的初始值
    sem(int num) {
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }

    // 析构函数，销毁信号量
    ~sem() { sem_destroy(&m_sem); }

    // 等待信号量（P操作）
    // 如果信号量的值大于0，则减1并返回
    // 如果信号量的值为0，则阻塞等待
    bool wait() {
        return sem_wait(&m_sem) == 0;  // 成功返回true，失败返回false
    }

    // 释放信号量（V操作）
    // 将信号量的值加1，唤醒等待的线程
    bool post() { return sem_post(&m_sem) == 0; }

   private:
    sem_t m_sem;  // POSIX信号量
};

// 互斥锁类
class locker {
   public:
    // 构造函数，初始化互斥锁
    locker() {
        // pthread_mutex_init参数：
        // 1. 指向互斥锁对象的指针
        // 2. 指向互斥锁属性对象的指针，NULL表示默认属性
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();  // 初始化失败抛出异常
        }
    }

    // 析构函数，销毁互斥锁
    ~locker() { pthread_mutex_destroy(&m_mutex); }

    // 加锁
    bool lock() { return pthread_mutex_lock(&m_mutex) == 0; }

    // 解锁
    bool unlock() { return pthread_mutex_unlock(&m_mutex) == 0; }

    // 获取互斥锁指针
    pthread_mutex_t *get() { return &m_mutex; }

   private:
    pthread_mutex_t m_mutex;  // POSIX互斥锁
};

// 条件变量类
class cond {
   public:
    // 构造函数，初始化条件变量
    cond() {
        // pthread_cond_init参数：
        // 1. 指向条件变量对象的指针
        // 2. 指向条件变量属性对象的指针，NULL表示默认属性
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();  // 初始化失败抛出异常
        }
    }

    // 析构函数，销毁条件变量
    ~cond() { pthread_cond_destroy(&m_cond); }

    // 等待条件变量
    // 1. 调用前必须确保mutex已加锁
    // 2. 函数会原子地释放mutex并进入等待
    // 3. 被唤醒后会重新获取mutex
    bool wait(pthread_mutex_t *m_mutex) {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }

    // 带超时的等待条件变量
    // t: 指定等待的超时时间
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t) {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }

    // 唤醒一个等待该条件变量的线程
    bool signal() { return pthread_cond_signal(&m_cond) == 0; }

    // 唤醒所有等待该条件变量的线程
    bool broadcast() { return pthread_cond_broadcast(&m_cond) == 0; }

   private:
    pthread_cond_t m_cond;  // POSIX条件变量
};

#endif