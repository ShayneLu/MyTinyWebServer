/*
 * 日志系统实现文件
 * 功能：提供日志记录功能，支持同步和异步日志写入。
 * 同步模式：日志直接写入文件。
 * 异步模式：日志通过阻塞队列缓冲，由后台线程异步写入文件，避免阻塞主线程。
 * 支持按日期和行数分割日志文件，便于管理和查看。
 */

#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "log.h"
using namespace std;

// 构造函数，初始化日志计数器并设置异步标志为false
Log::Log() {
    m_count = 0;         // 日志行数计数器
    m_is_async = false;  // 默认同步模式
}

// 析构函数，关闭日志文件
Log::~Log() {
    if (m_fp != NULL) {
        fclose(m_fp);  // 关闭文件指针
    }
}

/*
 * 初始化日志系统
 * 参数：
 *   - file_name: 日志文件名
 *   - close_log: 是否关闭日志（未使用）
 *   - log_buf_size: 日志缓冲区大小
 *   - split_lines: 日志文件最大行数，超过则分割
 *   - max_queue_size: 异步队列大小，>=1时启用异步模式
 * 返回值：成功返回true，失败返回false
*/
bool Log::init(const char *file_name, int close_log, int log_buf_size,
               int split_lines, int max_queue_size) {
    // 如果设置了max_queue_size，则启用异步模式
    if (max_queue_size >= 1) {
        m_is_async = true;  // 设置为异步模式
        m_log_queue = new block_queue<string>(max_queue_size);  // 创建阻塞队列
        pthread_t tid;  //创建线程标识符，唯一标识该线程，类似进程的pid
        // 创建线程异步写日志，flush_log_thread为回调函数
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;              // 是否关闭日志（未使用）
    m_log_buf_size = log_buf_size;        // 设置日志缓冲区大小
    m_buf = new char[m_log_buf_size];     // 分配缓冲区内存
    memset(m_buf, '\0', m_log_buf_size);  // 初始化缓冲区
    m_split_lines = split_lines;          // 设置日志文件最大行数

    // 获取当前时间
    // time_t t = time(NULL);
    // struct tm *sys_tm = localtime(&t);
    // struct tm my_tm = *sys_tm;
    time_t t = time(NULL); /* 获取当前的系统时间，返回的是从 1970 年 1 月 1日（即 Unix 纪元）到现在的秒数 */
    struct tm my_tm; /* 包含了当前的本地时间信息 */
    if (localtime_r(&t, &my_tm) == nullptr) {
        std::cerr << "Failed to get local time." << std::endl;
        return false;
    }
    // 解析日志文件名和路径
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL)  // 如果文件名中不包含路径
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else  // 如果文件名中包含路径
    {
        strcpy(log_name, p + 1);                          // 提取文件名
        strncpy(dir_name, file_name, p - file_name + 1);  // 提取路径
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name,
                 my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                 log_name);
    }

    m_today = my_tm.tm_mday;  // 记录当前日期

    // 打开日志文件
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL) {
        return false;  // 文件打开失败
    }

    return true;  // 初始化成功
}

/*
 * 写入日志
 * 参数：
 *   - level: 日志级别（0: debug, 1: info, 2: warn, 3: error）
 *   - format: 格式化字符串
 *   - ...: 可变参数
*/
void Log::write_log(int level, const char *format, ...) {

    // 获取当前时间（精确到微秒）
    struct timeval now = {0, 0};       // 定义timeval结构体，用于存储秒和微秒
    gettimeofday(&now, NULL);          // 获取当前系统时间（比time()更高精度）
    
    time_t t = now.tv_sec;             // 提取秒级时间
    struct tm *sys_tm = localtime(&t); // 转换为本地时间的tm结构体
    struct tm my_tm = *sys_tm;         // 拷贝时间结构体（避免localtime的线程安全问题）
    
    char s[16] = {0};                  // 准备存储日志级别前缀的缓冲区

    // 根据日志级别设置前缀
    switch (level) {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    // 经过以上操作，产生的日志头部如下：
    // 2023-10-05 14:30:45.123456 [debug]: 这是一条调试日志
    
    
/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：
    // 加锁，使用locker机制来确保线程安全的日志文件分割/
    m_mutex.lock();
    m_count++;  // 日志行数计数器递增。同时表明，每次调用write_log，只写入1行日志
    // 每次调用write_log，都会检查是否需要分割
    // 检查是否需要分割日志文件（按日期或行数，初始化日期不等于当前日期表明跨天，行数超过限制）
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_fp);  // 刷新缓冲区
        fclose(m_fp);  // 关闭当前文件
        char tail[16] = {0};

        // 生成新的日志文件名
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday)  // 如果是新的一天
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;  // 更新日期
            m_count = 0;              // 重置计数器
        } else                        // 如果是行数超过限制
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name,
                     m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");  // 打开新文件
    }

    m_mutex.unlock();
/*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/

    // 处理可变参数。即调用write_log时，可以像printf一样，传入格式化字符串和可变参数
    // 如：log->write_log(1, "User %s login from %s", username, ip);
    va_list valst;          // 定义可变参数列表指针
    va_start(valst, format); // 初始化valst，指向format后的第一个可变参数



/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：
    string log_str; //存储最终格式化后的完整日志内容，即要写入日志文件的日志行
    // 格式示例：2023-10-05 14:30:45.123456 [info]: User login from 192.168.1.1
    m_mutex.lock();

    // 格式化时间戳和日志内容，char*指针m_buf是日志缓冲区，暂存日志内容
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 拼接为完整日志行
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();
/*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/

    // // 根据模式选择写入方式
    // if (m_is_async && !m_log_queue->full())  // 异步模式且队列未满
    // {
    //     m_log_queue->push(log_str);  // 将日志推入队列
    // } else                           // 同步模式或队列已满
    // {   //操作共享资源m_fp，故需加锁
    //     m_mutex.lock();
    //     fputs(log_str.c_str(), m_fp);  // 直接写入文件
    //     m_mutex.unlock();
    // }

    if (m_is_async && !m_log_queue->full()) {   // 异步模式且队列未满
        m_log_queue->push(log_str); /* 将日志字符串 log_str 推入日志队列 */
    } else {
        /* 如果队列已满，选择丢弃当前日志，并增加 dropped_logs 计数器。 */
        static int dropped_logs = 0;
        if (m_is_async && m_log_queue->full()) {
            dropped_logs++;
            if (dropped_logs % 100 == 0) {
                // 每丢弃 100条日志时，通过标准错误流（std::cerr）输出警告，提醒队列已满
                std::cerr << "Log queue full. Dropped logs count: "<< dropped_logs << std::endl;
            }
        } else {/* 同步写入日志文件 */
            std::lock_guard<locker> lock(m_mutex);
            fputs(log_str.c_str(), m_fp);
        }
    }

    va_end(valst);  // 结束可变参数处理
}

/*
 * flush() 函数用于强制刷新日志文件的缓冲区，确保所有缓存的日志数据立即写入磁盘文件。
 */
void Log::flush(void) {
    m_mutex.lock();   //操作共享资源m_fp，故需加锁
    fflush(m_fp);  // 刷新文件缓冲区// 调用C标准库函数，刷新文件流缓冲区
    m_mutex.unlock();
}
