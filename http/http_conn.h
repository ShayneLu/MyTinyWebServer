// http_conn.h 文件定义了一个 http_conn 类，类包含了与http_conn对象交互的函数、内部解析HTTP请求与生成响应的函数、这些方法涉及的变量，涵盖了从连接创建、与连接交互到读取请求、解析请求、处理请求到发送响应的完整流程。通过这个类，Web 服务器可以高效地处理多个并发连接。

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <arpa/inet.h>      // 包含网络相关的头文件，用于处理IP地址和端口，如inet_pton()、inet_ntop()  
#include <assert.h>         // 包含断言相关的头文件，用于调试
#include <errno.h>          // 包含错误相关的头文件，用于处理错误，如errno
#include <fcntl.h>          // 包含文件控制相关的头文件，用于处理文件描述符，如fcntl()
#include <netinet/in.h>     // 包含网络相关的头文件，用于处理IP地址和端口，如sockaddr_in
#include <pthread.h>        // 包含线程相关的头文件，用于处理线程，如pthread_create()
#include <signal.h>         // 包含信号相关的头文件，用于处理信号，如signal()
#include <stdarg.h>         // 包含可变参数相关的头文件，用于处理可变参数，如printf()
#include <stdio.h>          // 包含标准输入输出相关的头文件，用于处理标准输入输出，如printf()
#include <stdlib.h>         // 包含标准库相关的头文件，用于处理标准库，如malloc()、free()
#include <string.h>         // 包含字符串处理函数，用于处理字符串，如strlen()   
#include <sys/epoll.h>      // 包含epoll相关的头文件，用于处理epoll，如epoll_create()
#include <sys/mman.h>       // 包含内存映射相关的头文件，用于处理内存映射，如mmap()
#include <sys/socket.h>     // 包含socket相关的头文件，用于处理socket，如socket()
#include <sys/stat.h>        // 包含文件状态相关的头文件，用于处理文件状态，如stat()
#include <sys/types.h>       // 包含基本数据类型定义，如 size_t
#include <sys/uio.h>         // 包含分散/聚集IO相关的头文件，用于处理分散/聚集IO，如writev()
#include <sys/wait.h>        // 包含进程等待相关的函数，用于处理进程等待，如wait()
#include <unistd.h>          // 包含 POSIX 标准函数，如 close()，用于处理unistd，如read()、write()

#include <map>   // 包含 C++ STL 中的 map 容器。

#include "../CGImysql/sql_connection_pool.h"    //包含数据库连接池类
#include "../lock/locker.h"                      //包含锁类，用于线程同步
#include "../log/log.h"                          //包含日志类
#include "../timer/lst_timer.h"                  //包含定时器类，用于处理非活跃连接

class http_conn {
   public:
    static const int FILENAME_LEN = 200;        // 文件名最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区大小

    // HTTP请求方法枚举。里面大部分是HTTP/1.1协议中要求的内容
    enum METHOD {
        GET = 0,  // GET请求
        POST,     // POST请求
        HEAD,     // HEAD请求
        PUT,      // PUT请求
        DELETE,   // DELETE请求
        TRACE,    // TRACE请求
        OPTIONS,  // OPTIONS请求
        CONNECT,  // CONNECT请求
        PATH      // PATH请求
    };

    // HTTP请求解析状态枚举，这个没有协议标准，是自定义的。
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,  // 正在解析请求行
        CHECK_STATE_HEADER,           // 正在解析请求头
        CHECK_STATE_CONTENT           // 正在解析请求体
    };

    // HTTP响应状态码枚举，是自定义的，
    // 但对应HTTP/1.1协议中的状态码，如NO_RESOURCE对应404 Not Found
    enum HTTP_CODE {
        NO_REQUEST,         // 无请求
        GET_REQUEST,        // 获取请求成功
        BAD_REQUEST,        // 错误请求
        NO_RESOURCE,        // 资源不存在
        FORBIDDEN_REQUEST,  // 禁止访问
        FILE_REQUEST,       // 文件请求
        INTERNAL_ERROR,     // 内部错误
        CLOSED_CONNECTION   // 连接关闭
    };

    // 行解析状态枚举，用于解析HTTP请求中的每一行。
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

   public:
    http_conn() {}   // 构造函数，本项目http_conn只有默认构造函数
    ~http_conn() {}  // 析构函数，本项目http_conn只有默认析构函数

/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：与http_conn对象交互相关的函数，如初始化、关闭连接、读取数据、写入数据等。
   public:
    // 初始化函数，设置socket、地址、用户信息等
    void init(int sockfd, const sockaddr_in &addr, char *, int, int,
              string user, string passwd, string sqlname);
    // 关闭连接
    void close_conn(bool real_close = true);
    // 有这read_once()、process()、write()三个接口函数，意味着可以使用线程池threadpool。
    // 处理HTTP请求
    void process();
    // 读取数据函数，用于从socket中读取数据到读缓冲区。
    bool read_once();
    // 写入数据，用于将写缓冲区中的数据写入socket。
    bool write();
    // 获取地址，获取的是客户端的地址信息
    sockaddr_in *get_address() { return &m_address; }
    // 初始化MySQL结果
    void initmysql_result(connection_pool *connPool);
    
    int timer_flag;  // 定时器标志，其值为1表示需要关闭连接（或定时器处理）
    int improv;      // 改进标志，其值为1表示需要改进（或已处理）
 /*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/


/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：类的内部实现细节，如解析 HTTP 请求、生成 HTTP 响应、管理内存映射等。
   private:
    // 初始化函数
    void init();

    // 处理读取的HTTP请求，解析请求行、请求头和请求体。
    HTTP_CODE process_read();
     // 获取当前行，获取当前行。
    char *get_line() { return m_read_buf + m_start_line; };
    // 解析行，用于解析HTTP请求中的每一行。
    LINE_STATUS parse_line();   

    // 解析请求行，解析HTTP请求的第一行，即请求行。
    HTTP_CODE parse_request_line(char *text);
    // 解析请求头，解析HTTP请求的头部信息。
    HTTP_CODE parse_headers(char *text);
    // 解析请求体，解析HTTP请求的请求体。
    HTTP_CODE parse_content(char *text);
    // 处理请求，根据请求方法执行相应的操作。
    HTTP_CODE do_request();


    // 解除内存映射，释放文件映射的内存。被映射的文件是静态文件，如html、css、js等。
    void unmap();

    // 处理写入的HTTP响应，，根据解析结果生成响应内容。
    bool process_write(HTTP_CODE ret);
    // 添加响应内容，，用于生成HTTP响应。
    bool add_response(const char *format, ...);
    // 添加内容，用于生成HTTP响应的内容部分。
    bool add_content(const char *content);
    // 添加状态行，用于生成HTTP响应的状态行。
    bool add_status_line(int status, const char *title);
    // 添加头部信息，用于生成HTTP响应的头部。
    bool add_headers(int content_length);
    // 添加内容类型，用于生成HTTP响应的内容类型。
    bool add_content_type();
    // 添加内容长度，用于生成HTTP响应的内容长度。
    bool add_content_length(int content_length);
    // 添加连接状态，用于生成HTTP响应的连接状态。
    bool add_linger();
    // 添加空行，用于生成HTTP响应的空行。
    bool add_blank_line();

/*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/



   public:
    static int m_epollfd;     // epoll文件描述符，被所有连接(其实每个连接都是一个http_conn对象)共享，值来自webserver类
    static int m_user_count;  // 用户数量，其实是http_conn对象的数量
    MYSQL *mysql;             // MySQL连接，不为每个连接单独创建一个 MySQL 连接，它的值来自连接池
    int m_state;              // 状态，0表示读，1表示写

   private:
    int m_sockfd;                         // socket文件描述符
    sockaddr_in m_address;                // 地址信息，存储客户端的 IP 地址和端口号。
    char m_read_buf[READ_BUFFER_SIZE];    // 读缓冲区，用于存储从客户端读取的数据。
    long m_read_idx;                      // 读索引，表示当前读取的位置。
    long m_checked_idx;                   // 已检查索引，表示已解析的数据位置。
    int m_start_line;                     // 行起始位置，表示当前行的起始位置。
    char m_write_buf[WRITE_BUFFER_SIZE];  // 写缓冲区，用于存储要发送给客户端的数据。
    int m_write_idx;                      // 写索引，表示当前写入的位置。
    CHECK_STATE m_check_state;            // 检查状态，表示当前解析的状态（请求行、请求头、请求体）。
    METHOD m_method;                      // 请求方法，如 GET、POST 等。
    char m_real_file[FILENAME_LEN];       // 实际文件路径，存储请求的文件路径。
    char *m_url;                          // URL，存储请求的 URL。
    char *m_version;                      // HTTP版本，如 HTTP/1.1。
    char *m_host;                         // 主机名，存储请求的主机名。
    long m_content_length;                // 内容长度，表示请求体的长度。
    bool m_linger;                        // 是否保持连接，表示客户端是否希望保持连接。
    char *m_file_address;                 // 文件地址，存储文件的内存映射地址。
    struct stat m_file_stat;              // 文件状态，存储文件的状态信息。
    struct iovec m_iv[2];                 // 分散/聚集IO向量，用于高效地发送数据。
    int m_iv_count;                       // IO向量数量，表示 m_iv 数组中的有效元素数量。
    int cgi;                              // 是否启用POST，表示是否启用 CGI 处理。
    char *m_string;                       // 存储请求头数据，用于解析请求头。
    int bytes_to_send;                    // 待发送字节数，表示还需要发送的字节数。
    int bytes_have_send;                  // 已发送字节数，表示已经发送的字节数。
    char *doc_root;                       // 文档根目录，存储服务器的根目录路径。

    map<string, string> m_users;  // 用户信息，存储用户的数据。
    int m_TRIGMode;               // 触发模式，表示 epoll 的触发模式（ET或LT）。
    int m_close_log;              // 是否关闭日志

    char sql_user[100];    // SQL用户名，存储数据库的用户名。
    char sql_passwd[100];  // SQL密码，存储数据库的密码。
    char sql_name[100];    // SQL数据库名，存储数据库的名称。
};

#endif
