/**
 * @file sql_connection_pool.cpp
 * @brief 数据库连接池的实现文件
 *
 * 该文件实现了一个MySQL数据库连接池，用于管理和复用数据库连接。
 * 主要功能包括：
 * 1. 创建和管理数据库连接池
 * 2. 提供连接的获取和释放机制
 * 3. 使用RAII模式自动管理连接资源
 * 4. 实现线程安全的连接池操作
 */

#include <mysql/mysql.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <list>
#include <string>

#include "sql_connection_pool.h"

using namespace std;

/**
 * @brief 连接池构造函数
 * 初始化连接池的基本参数
 */
connection_pool::connection_pool() {
    m_CurConn = 0;   // 当前已使用的连接数
    m_FreeConn = 0;  // 当前空闲的连接数
}

/**
 * @brief 获取连接池单例
 * @return 返回连接池的唯一实例
 */
connection_pool *connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

/**
 * @brief 初始化数据库连接池
 * @param url 数据库主机地址
 * @param User 数据库用户名
 * @param PassWord 数据库密码
 * @param DBName 数据库名称
 * @param Port 数据库端口号
 * @param MaxConn 最大连接数
 * @param close_log 日志开关
 */
void connection_pool::init(string url, string User, string PassWord,
                           string DBName, int Port, int MaxConn,
                           int close_log) {
    m_url = url;              // 数据库主机地址
    m_Port = Port;            // 数据库端口号
    m_User = User;            // 数据库用户名
    m_PassWord = PassWord;    // 数据库密码
    m_DatabaseName = DBName;  // 数据库名称
    m_close_log = close_log;  // 日志开关

    // 创建MaxConn个数据库连接
    for (int i = 0; i < MaxConn; i++) {
        MYSQL *con = NULL;
        con = mysql_init(con);  // 初始化MySQL连接

        // if (con == NULL) {
        //     LOG_ERROR("MySQL Error");
        //     exit(1);
        // }
        if (con == NULL) {
            LOG_ERROR("MySQL init Error: %s", mysql_error(con));
            continue; /* 记录错误但不退出程序 */
        }
        // 建立实际的数据库连接
        con =
            mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(),
                               DBName.c_str(), Port, NULL, 0);

        // if (con == NULL) {
        //     LOG_ERROR("MySQL Error");
        //     exit(1);
        // }
        if (con == NULL) {
            LOG_ERROR("MySQL init Error: %s", mysql_error(con));
            mysql_close(con); /* 释放当前连接资源 */
            continue;         /* 记录错误但不退出程序 */
        }
        connList.push_back(con);  // 将连接添加到连接池
        ++m_FreeConn;             // 空闲连接数加1
    }

    reserve = sem(m_FreeConn);  // 初始化信号量，初始值为空闲连接数
    m_MaxConn = m_FreeConn;     // 设置最大连接数

    /* 如果没有任何连接被成功创建 */
    if (m_FreeConn == 0) {
        LOG_ERROR("All database connections failed to initialize.");
        exit(1); //完全失败，没有连接被创建成功，才退出程序
    }
}

/**
 * @brief 获取数据库连接
 * @return 返回一个可用的数据库连接
 */
/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：这里可以改进
MYSQL *connection_pool::GetConnection() {
    MYSQL *con = NULL;

    if (0 == connList.size()) return NULL;

    reserve.wait();  // 等待信号量，确保有可用连接

    lock.lock();  // 加锁保护连接池操作

    con = connList.front();  // 获取连接池中的第一个连接
    connList.pop_front();    // 从连接池中移除该连接

    --m_FreeConn;  // 空闲连接数减1
    ++m_CurConn;   // 已使用连接数加1

    lock.unlock();  // 解锁
    return con;
}

// std::shared_ptr<MYSQL> connection_pool::GetConnection() {
//     reserve.wait(); /* 阻塞等待信号量，确保有可用连接 */

//     lock.lock();

//     if (connList.empty()) {
//         lock.unlock();
//         return nullptr;
//     }

//     std::shared_ptr<MYSQL> con = connList.front();
//     connList.pop_front();

//     --m_FreeConn;
//     ++m_CurConn;

//     lock.unlock();

//     return con;
// }
/*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/

/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：RAII机制的原理
// class ConnectionRAII {
// private:
//     MYSQL *conRAII;             // 资源句柄，获取的资源赋值给它
//     connection_pool *poolRAII;  // 由于本例中析构资源需要资源池指针，所以才定义
// public:
//     // 构造函数获取资源
//     ConnectionRAII(MYSQL **con, connection_pool *connPool) {
//         *con = connPool->GetConnection();  // 关键点1：构造时获取连接
//         conRAII = *con; // 将资源分配给资源变量
//         poolRAII = connPool;
//     }
//     // 析构函数释放资源
//     ~ConnectionRAII() {
//         poolRAII->ReleaseConnection(conRAII);  // 关键点2：析构时自动释放
//     }

//     // 外界获取资源的接口函数，可以用get()，或者->来访问资源
//     MYSQL *get() const { return conRAII; }
//     MYSQL *operator->() const { assert(conRAII != nullptr); return conRAII; }
//     // 禁用拷贝（避免重复释放）
//     ConnectionRAII(const ConnectionRAII &) = delete;
//     ConnectionRAII &operator=(const ConnectionRAII &) = delete;
//     // 允许移动语义
//     ConnectionRAII(ConnectionRAII &&rhs) noexcept
//         : poolRAII(rhs.pool_), conRAII(rhs.conn_) { rhs.conRAII = nullptr; }
// };

/*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/


/**
 * @brief 释放数据库连接
 * @param con 要释放的数据库连接
 * @return 释放是否成功
 */
bool connection_pool::ReleaseConnection(MYSQL *con) {
    if (NULL == con) return false;

    lock.lock();  // 加锁保护连接池操作

    connList.push_back(con);  // 将连接放回连接池
    ++m_FreeConn;             // 空闲连接数加1
    --m_CurConn;              // 已使用连接数减1

    lock.unlock();  // 解锁

    reserve.post();  // 释放信号量
    return true;
}

/**
 * @brief 销毁数据库连接池
 * 关闭所有数据库连接并清空连接池
 */
void connection_pool::DestroyPool() {
    lock.lock();  // 加锁保护连接池操作
    if (connList.size() > 0) {
        list<MYSQL *>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);  // 关闭数据库连接
        }
        m_CurConn = 0;     // 重置已使用连接数
        m_FreeConn = 0;    // 重置空闲连接数
        connList.clear();  // 清空连接池
    }
    lock.unlock();  // 解锁
}

/**
 * @brief 获取当前空闲连接数
 * @return 返回空闲连接数
 */
int connection_pool::GetFreeConn() { return this->m_FreeConn; }

/**
 * @brief 连接池析构函数
 * 销毁连接池中的所有连接
 */
connection_pool::~connection_pool() { DestroyPool(); }

/**
 * @brief RAII类构造函数
 * @param SQL 数据库连接指针的指针
 * @param connPool 连接池指针
 */
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();  // 从连接池获取连接

    conRAII = *SQL;       // 保存连接指针
    poolRAII = connPool;  // 保存连接池指针
}

/**
 * @brief RAII类析构函数
 * 自动释放数据库连接回连接池
 */
connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);  // 将连接释放回连接池
}