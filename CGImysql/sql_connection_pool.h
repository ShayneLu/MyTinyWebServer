/**
 * @file sql_connection_pool.h
 * @brief 数据库连接池的头文件
 *
 * 该文件定义了数据库连接池的类结构，实现了一个线程安全的MySQL数据库连接池。
 * 主要特点：
 * 1. 使用单例模式确保全局只有一个连接池实例
 * 2. 实现了RAII机制自动管理数据库连接
 * 3. 使用信号量和互斥锁保证线程安全
 * 4. 支持动态管理数据库连接的创建和销毁
 */

#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <error.h>
#include <mysql/mysql.h>
#include <stdio.h>
#include <string.h>

#include <iostream>
#include <list>
#include <string>

#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

/**
 * @brief 数据库连接池类
 *
 * 该类实现了数据库连接池的核心功能，包括：
 * 1. 连接的创建和初始化
 * 2. 连接的获取和释放
 * 3. 连接池的维护和管理
 * 4. 线程安全的连接操作
 */
class connection_pool {
   public:
    /**
     * @brief 获取数据库连接
     * @return 返回一个可用的数据库连接指针
     */
    MYSQL *GetConnection();

    /**
     * @brief 释放数据库连接
     * @param conn 要释放的数据库连接指针
     * @return 释放是否成功
     */
    bool ReleaseConnection(MYSQL *conn);

    /**
     * @brief 获取当前空闲连接数
     * @return 返回连接池中空闲连接的数量
     */
    int GetFreeConn();

    /**
     * @brief 销毁连接池中的所有连接
     */
    void DestroyPool();

    /**
     * @brief 获取连接池单例
     * @return 返回连接池的唯一实例
     */
    static connection_pool *GetInstance();

    /**
     * @brief 初始化数据库连接池
     * @param url 数据库主机地址
     * @param User 数据库用户名
     * @param PassWord 数据库密码
     * @param DataBaseName 数据库名称
     * @param Port 数据库端口号
     * @param MaxConn 最大连接数
     * @param close_log 日志开关
     */
    void init(string url, string User, string PassWord, string DataBaseName,
              int Port, int MaxConn, int close_log);

   private:
    /**
     * @brief 连接池构造函数
     * 私有构造函数，防止外部创建实例
     */
    connection_pool();

    /**
     * @brief 连接池析构函数
     * 私有析构函数，防止外部删除实例
     */
    ~connection_pool();

    int m_MaxConn;           // 连接池最大连接数
    int m_CurConn;           // 当前已使用的连接数
    int m_FreeConn;          // 当前空闲的连接数
    locker lock;             // 互斥锁，用于保护连接池的并发访问
    list<MYSQL *> connList;  // 连接池，存储所有数据库连接
    // list<std::shared_ptr<MYSQL>> connList; // 改进：智能指针列表
    sem reserve;             // 信号量，用于控制连接池的并发访问

   public:
    string m_url;           // 数据库主机地址
    string m_Port;          // 数据库端口号
    string m_User;          // 数据库用户名
    string m_PassWord;      // 数据库密码
    string m_DatabaseName;  // 数据库名称
    int m_close_log;        // 日志开关（0开启/1关闭）
};

/**
 * @brief RAII类，用于自动管理数据库连接
 *
 * 该类使用RAII（Resource Acquisition Is Initialization）模式，
 * 在构造时获取数据库连接，在析构时自动释放连接回连接池
 */
class connectionRAII {
   public:
    /**
     * @brief RAII类构造函数
     * @param con 数据库连接指针的指针
     * @param connPool 连接池指针
     */
    connectionRAII(MYSQL **con, connection_pool *connPool);

    /**
     * @brief RAII类析构函数
     * 自动释放数据库连接回连接池
     */
    ~connectionRAII();

   private:
    MYSQL *conRAII;             // 保存的数据库连接指针
    connection_pool *poolRAII;  // 保存的连接池指针
};

#endif
