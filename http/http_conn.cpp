// http_conn.cpp 文件实现了 http_conn 类的具体功能，包括处理 HTTP 请求、解析请求行、请求头和请求体、生成 HTTP 响应、管理内存映射等。通过这个类，Web 服务器可以高效地处理多个并发连接。


#include <mysql/mysql.h>  // 包含 MySQL 相关的头文件，用于数据库操作

#include <fstream>  // 包含文件流操作相关的头文件，用于文件读写

#include "http_conn.h"  // 包含 http_conn 类的头文件

// 定义 HTTP 响应的一些状态信息
const char *ok_200_title = "OK";              // HTTP 200 响应的状态信息
const char *error_400_title = "Bad Request";  // HTTP 400 响应的状态信息
const char *error_400_form =
    "Your request has bad syntax or is inherently impossible to "
    "staisfy.\n";                           // HTTP 400 响应的详细描述
const char *error_403_title = "Forbidden";  // HTTP 403 响应的状态信息
const char *error_403_form =
    "You do not have permission to get file form this server.\n";  // HTTP 403

const char *error_404_title = "Not Found";  // HTTP 404 响应的状态信息
const char *error_404_form =
    "The requested file was not found on this server.\n";  // HTTP 404

const char *error_500_title = "Internal Error";  // HTTP 500 响应的状态信息
const char *error_500_form =
    "There was an unusual problem serving the request "
    "file.\n";  // HTTP 500 响应的详细描述

locker m_lock;              // 定义互斥锁，用于线程同步
map<string, string> users;  // 定义用户信息映射，存储用户名和密码

/*↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓*/
//代码块功能：对两个重要的类内静态变量的初始化
int http_conn::m_user_count = 0;  // 初始化用户数量为 0
int http_conn::m_epollfd = -1;    // 初始化 epoll 文件描述符为 -1
/*↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑*/


// 从数据库中检索用户信息（用户名和密码），并将其存储在全局的 users 映射中，以便后续的登录和注册操作可以快速验证用户身份。
void http_conn::initmysql_result(connection_pool *connPool) {
    // 先从连接池中取一个连接
    MYSQL *mysql = NULL;//根据下面代码，mysql就是获取的连接池中的一个连接，connPool是连接池的指针
    connectionRAII mysqlcon(&mysql, connPool);  // 使用 RAII 机制管理数据库连接。

    // 在 user 表中检索 username 和 passwd 数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_errno(mysql),
                  mysql_error(mysql));  // 如果查询失败，记录错误日志
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码存入 map 中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);  // 获取用户名
        string temp2(row[1]);  // 获取密码
        users[temp1] = temp2;  // 将用户名和密码存入 users 映射中
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);  // 获取文件描述符的当前标志
    int new_option = old_option | O_NONBLOCK;  // 设置非阻塞标志
    fcntl(fd, F_SETFL, new_option);            // 应用新的标志
    return old_option;                         // 返回旧的标志
}

    // 事件标志解释：
    // EPOLLIN：表示对应的文件描述符可以读（包括对端 socket 正常关闭）。
    // EPOLLOUT：表示对应的文件描述符可以写。
    // EPOLLERR：表示对应的文件描述符发生错误。
    // EPOLLHUP：表示对应的文件描述符被挂起（对端关闭连接）。
    // EPOLLRDHUP：表示对端 socket 关闭连接或关闭写操作。
    // EPOLLET：将 epoll 设置为边缘触发模式（Edge Triggered）。
    // EPOLLONESHOT：表示只监听一次事件，当事件发生后，需要重新注册事件。
// 将内核事件表注册读事件，ET 模式，选择开启 EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;  //epoll_event是结构体，里面有events和data两个成员，events是事件类型，data是事件数据
    event.data.fd = fd;  // 设置事件的文件描述符
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // 监听可读事件，使用边缘触发模式，监听对端关闭连接事件。
    else
        event.events = EPOLLIN | EPOLLRDHUP;  // 水平触发模式

    if (one_shot)
        event.events |= EPOLLONESHOT;  // 如果开启 EPOLLONESHOT，设置相应标志
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);  // 将事件添加到 epoll 实例中
    setnonblocking(fd);  // 设置文件描述符为非阻塞
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);  // 从 epoll 实例中删除文件描述符
    close(fd);                                 // 关闭文件描述符
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) {//ev是事件类型，TRIGMode是触发模式，0表示水平触发，1表示边缘触发
    epoll_event event;
    event.data.fd = fd;  // 设置事件的文件描述符

    if (1 == TRIGMode)
        event.events =
            ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;  // 边缘触发模式
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;  // 水平触发模式

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);  // 修改 epoll 实例中的事件
}

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);  // 打印关闭的连接
        removefd(m_epollfd, m_sockfd);  // 从 epoll 实例中删除文件描述符
        m_sockfd = -1;                  // 将文件描述符设置为 -1
        m_user_count--;                 // 用户数量减一
    }
}

// 通过初始化，http_conn 对象可以：
// •	连接准备就绪：socket 文件描述符已设置，添加到 epoll 实例中，对象的连接准备就绪，可以开始监听事件。
// •	请求处理状态重置：所有与 HTTP 请求处理相关的变量和缓冲区都被重置，对象的HTTP请求相关状态已重置，可以开始处理新的 HTTP 请求。
// •	配置和数据库信息已设置：文档根目录、触发模式、日志状态等配置已设置，数据库连接信息已保存，配置和数据库已就绪，可以在处理请求时使用。

void http_conn::init(int sockfd, const sockaddr_in &addr, char *root,
                     int TRIGMode, int close_log, string user, string passwd,
                     string sqlname) {
    m_sockfd = sockfd;  // 设置 socket 文件描述符
    m_address = addr;   // 设置地址信息

    addfd(m_epollfd, sockfd, true,
          m_TRIGMode);  // 将 socket 添加到 epoll 实例中
    m_user_count++;     // 用户数量加一

    // 当浏览器出现连接重置时，可能是网站根目录出错或 http
    // 响应格式出错或者访问的文件中内容完全为空
    doc_root = root;          // 设置文档根目录
    m_TRIGMode = TRIGMode;    // 设置触发模式
    m_close_log = close_log;  // 设置是否关闭日志

    strcpy(sql_user, user.c_str());      // 设置 SQL 用户名
    strcpy(sql_passwd, passwd.c_str());  // 设置 SQL 密码
    strcpy(sql_name, sqlname.c_str());   // 设置 SQL 数据库名

    init();  // 调用内部初始化函数
}

// 初始化新接受的连接
// check_state 默认为分析请求行状态
void http_conn::init() {
    mysql = NULL;         // 初始化 MySQL 连接为 NULL
    bytes_to_send = 0;    // 初始化待发送字节数为 0
    bytes_have_send = 0;  // 初始化已发送字节数为 0
    m_check_state = CHECK_STATE_REQUESTLINE;  // 设置检查状态为解析请求行
    m_linger = false;      // 初始化是否保持连接为 false
    m_method = GET;        // 初始化请求方法为 GET
    m_url = 0;             // 初始化 URL 为 NULL
    m_version = 0;         // 初始化 HTTP 版本为 NULL
    m_content_length = 0;  // 初始化内容长度为 0
    m_host = 0;            // 初始化主机为 NULL
    m_start_line = 0;      // 初始化行起始位置为 0
    m_checked_idx = 0;     // 初始化已检查索引为 0
    m_read_idx = 0;        // 初始化读索引为 0
    m_write_idx = 0;       // 初始化写索引为 0
    cgi = 0;               // 初始化是否启用 CGI 为 0
    m_state = 0;     // 初始化状态为 0（0 表示读，1 表示写）
    timer_flag = 0;  // 初始化定时器标志为 0
    improv = 0;      // 初始化改进标志为 0

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);    // 清空读缓冲区
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);  // 清空写缓冲区
    memset(m_real_file, '\0', FILENAME_LEN);       // 清空实际文件路径
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有 LINE_OK, LINE_BAD, LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];  // 获取当前字符
        if (temp == '\r') {                // 如果当前字符是回车符
            if ((m_checked_idx + 1) ==
                m_read_idx)        // 如果下一个字符是缓冲区末尾
                return LINE_OPEN;  // 返回行未完整
            else if (m_read_buf[m_checked_idx + 1] ==
                     '\n') {  // 如果下一个字符是换行符
                m_read_buf[m_checked_idx++] =
                    '\0';  // 将回车符替换为字符串结束符
                m_read_buf[m_checked_idx++] =
                    '\0';        // 将换行符替换为字符串结束符
                return LINE_OK;  // 返回行完整
            }
            return LINE_BAD;        // 返回行错误
        } else if (temp == '\n') {  // 如果当前字符是换行符
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] ==
                                         '\r') {  // 如果前一个字符是回车符
                m_read_buf[m_checked_idx - 1] =
                    '\0';  // 将回车符替换为字符串结束符
                m_read_buf[m_checked_idx++] =
                    '\0';        // 将换行符替换为字符串结束符
                return LINE_OK;  // 返回行完整
            }
            return LINE_BAD;  // 返回行错误
        }
    }
    return LINE_OPEN;  // 返回行未完整
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞 ET 工作模式下，需要一次性将数据读完
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) {  // 如果读缓冲区已满
        return false;                      // 返回读取失败
    }
    int bytes_read = 0;

    // LT 读取数据
    if (0 == m_TRIGMode) {  // 如果是水平触发模式
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);  // 读取数据
        m_read_idx += bytes_read;                             // 更新读索引

        if (bytes_read <= 0) {  // 如果读取的字节数小于等于 0
            return false;       // 返回读取失败
        }

        return true;  // 返回读取成功
    }
    // ET 读数据
    else {  // 如果是边缘触发模式
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                              READ_BUFFER_SIZE - m_read_idx, 0);  // 读取数据
            if (bytes_read == -1) {  // 如果读取失败
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;  // 如果是非阻塞模式下的 EAGAIN 或 EWOULDBLOCK
                            // 错误，跳出循环
                return false;              // 返回读取失败
            } else if (bytes_read == 0) {  // 如果读取的字节数为 0
                return false;              // 返回读取失败
            }
            m_read_idx += bytes_read;  // 更新读索引
        }
        return true;  // 返回读取成功
    }
}

// 解析 HTTP 请求行，获得请求方法，目标 URL 及 HTTP 版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");  // 查找第一个空格或制表符
    if (!m_url) {                  // 如果没有找到
        return BAD_REQUEST;        // 返回错误请求
    }
    *m_url++ = '\0';  // 将空格或制表符替换为字符串结束符，并移动指针
    char *method = text;                         // 获取请求方法
    if (strcasecmp(method, "GET") == 0)          // 如果是 GET 请求
        m_method = GET;                          // 设置请求方法为 GET
    else if (strcasecmp(method, "POST") == 0) {  // 如果是 POST 请求
        m_method = POST;                         // 设置请求方法为 POST
        cgi = 1;                                 // 启用 CGI
    } else
        return BAD_REQUEST;              // 返回错误请求
    m_url += strspn(m_url, " \t");       // 跳过空格或制表符
    m_version = strpbrk(m_url, " \t");   // 查找第一个空格或制表符
    if (!m_version) return BAD_REQUEST;  // 如果没有找到
    *m_version++ = '\0';  // 将空格或制表符替换为字符串结束符，并移动指针
    m_version += strspn(m_version, " \t");  // 跳过空格或制表符
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;  // 如果不是 HTTP/1.1 版本
    if (strncasecmp(m_url, "http://", 7) == 0) {  // 如果 URL 以 http:// 开头
        m_url += 7;                               // 跳过 http://
        m_url = strchr(m_url, '/');               // 查找第一个斜杠
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {  // 如果 URL 以 https:// 开头
        m_url += 8;                                // 跳过 https://
        m_url = strchr(m_url, '/');                // 查找第一个斜杠
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;  // 如果 URL 为空或不是以斜杠开头
    // 当 URL 为 / 时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");     // 如果 URL 为 /，显示 judge.html
    m_check_state = CHECK_STATE_HEADER;  // 设置检查状态为解析请求头
    return NO_REQUEST;                   // 返回无请求
}

// 解析 HTTP 请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if (text[0] == '\0') {                        // 如果当前行为空行
        if (m_content_length != 0) {              // 如果内容长度不为 0
            m_check_state = CHECK_STATE_CONTENT;  // 设置检查状态为解析请求体
            return NO_REQUEST;                    // 返回无请求
        }
        return GET_REQUEST;  // 返回获取请求成功
    } else if (strncasecmp(text, "Connection:", 11) ==
               0) {                   // 如果当前行是 Connection 头部
        text += 11;                   // 跳过 "Connection:"
        text += strspn(text, " \t");  // 跳过空格或制表符
        if (strcasecmp(text, "keep-alive") ==
            0) {              // 如果 Connection 为 keep-alive
            m_linger = true;  // 设置保持连接为 true
        }
    } else if (strncasecmp(text, "Content-length:", 15) ==
               0) {  // 如果当前行是 Content-length 头部
        text += 15;  // 跳过 "Content-length:"
        text += strspn(text, " \t");  // 跳过空格或制表符
        m_content_length = atol(text);  // 将内容长度转换为长整型并存储
    } else if (strncasecmp(text, "Host:", 5) == 0) {  // 如果当前行是 Host 头部
        text += 5;                                    // 跳过 "Host:"
        text += strspn(text, " \t");  // 跳过空格或制表符
        m_host = text;                // 存储主机名
    } else {
        LOG_INFO("oop!unknow header: %s", text);  // 记录未知头部信息
    }
    return NO_REQUEST;  // 返回无请求
}

// 判断 HTTP 请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >=
        (m_content_length +
         m_checked_idx)) {  // 如果已读取的数据长度大于等于内容长度
        text[m_content_length] = '\0';  // 在内容末尾添加字符串结束符
        // POST 请求中最后为输入的用户名和密码
        m_string = text;     // 存储请求体内容
        return GET_REQUEST;  // 返回获取请求成功
    }
    return NO_REQUEST;  // 返回无请求
}

// 处理读取的 HTTP 请求，解析请求行、请求头和请求体
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;  // 初始化行状态为 LINE_OK
    HTTP_CODE ret = NO_REQUEST;         // 初始化返回值为 NO_REQUEST
    char *text = 0;                     // 初始化文本指针
    // m_check_state是检查状态，表示当前解析的状态（请求行、请求头、请求体）。line_status是行状态，line_status = parse_line()是解析行，如果解析成功，则返回LINE_OK,如果解析失败，则返回LINE_BAD。
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK)) {  // 循环解析每一行
        text = get_line();                               // 获取当前行
        m_start_line = m_checked_idx;  // 更新行起始位置
        LOG_INFO("%s", text);          // 记录当前行
        switch (m_check_state) {       // 根据当前检查状态进行处理
            case CHECK_STATE_REQUESTLINE: {  // 如果当前状态是解析请求行
                ret = parse_request_line(text);  // 解析请求行
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;  // 如果解析失败，返回错误请求
                break;
            }
            case CHECK_STATE_HEADER: {  // 如果当前状态是解析请求头
                ret = parse_headers(text);  // 解析请求头
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;  // 如果解析失败，返回错误请求
                else if (ret == GET_REQUEST) {
                    return do_request();  // 如果解析成功，处理请求
                }
                break;
            }
            case CHECK_STATE_CONTENT: {  // 如果当前状态是解析请求体
                ret = parse_content(text);  // 解析请求体
                if (ret == GET_REQUEST)
                    return do_request();  // 如果解析成功，处理请求
                line_status = LINE_OPEN;  // 设置行状态为 LINE_OPEN
                break;
            }
            default:
                return INTERNAL_ERROR;  // 返回内部错误
        }
    }
    return NO_REQUEST;  // 返回无请求
}

// 处理请求，根据请求方法执行相应的操作
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);  // 将文档根目录复制到实际文件路径
    int len = strlen(doc_root);  // 获取文档根目录的长度
    // printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');  // 查找 URL 中最后一个斜杠

    // 处理 CGI
    if (cgi == 1 &&
        (*(p + 1) == '2' ||
         *(p + 1) == '3')) {  // 如果启用 CGI 并且 URL 以 /2 或 /3 结尾
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];  // 获取标志

        char *m_url_real = (char *)malloc(sizeof(char) * 200);  // 分配内存
        strcpy(m_url_real, "/");                                // 复制斜杠
        strcat(m_url_real, m_url + 2);                          // 拼接 URL
        strncpy(m_real_file + len, m_url_real,
                FILENAME_LEN - len - 1);  // 复制到实际文件路径
        free(m_url_real);                 // 释放内存

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];  // 定义用户名和密码数组
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];  // 提取用户名
        name[i - 5] = '\0';             // 添加字符串结束符

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];  // 提取密码
        password[j] = '\0';             // 添加字符串结束符

        if (*(p + 1) == '3') {  // 如果是注册
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);  // 分配内存
            strcpy(sql_insert,
                   "INSERT INTO user(username, passwd) VALUES(");  // 拼接 SQL
                                                                   // 插入语句
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {  // 如果用户名不存在
                m_lock.lock();                      // 加锁
                int res = mysql_query(mysql, sql_insert);  // 执行 SQL 插入语句
                users.insert(
                    pair<string, string>(name, password));  // 插入用户名和密码
                m_lock.unlock();                            // 解锁

                if (!res)
                    strcpy(m_url, "/log.html");  // 如果插入成功，跳转到登录页面
                else
                    strcpy(
                        m_url,
                        "/registerError.html");  // 如果插入失败，跳转到注册错误页面
            } else
                strcpy(
                    m_url,
                    "/registerError.html");  // 如果用户名已存在，跳转到注册错误页面
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2') {  // 如果是登录
            if (users.find(name) != users.end() &&
                users[name] == password)  // 如果用户名和密码匹配
                strcpy(m_url, "/welcome.html");  // 跳转到欢迎页面
            else
                strcpy(m_url, "/logError.html");  // 跳转到登录错误页面
        }
    }

    if (*(p + 1) == '0') {  // 如果 URL 以 /0 结尾
        char *m_url_real = (char *)malloc(sizeof(char) * 200);  // 分配内存
        strcpy(m_url_real, "/register.html");  // 复制注册页面路径
        strncpy(m_real_file + len, m_url_real,
                strlen(m_url_real));  // 复制到实际文件路径
        free(m_url_real);             // 释放内存
    } else if (*(p + 1) == '1') {     // 如果 URL 以 /1 结尾
        char *m_url_real = (char *)malloc(sizeof(char) * 200);  // 分配内存
        strcpy(m_url_real, "/log.html");  // 复制登录页面路径
        strncpy(m_real_file + len, m_url_real,
                strlen(m_url_real));  // 复制到实际文件路径
        free(m_url_real);             // 释放内存
    } else if (*(p + 1) == '5') {     // 如果 URL 以 /5 结尾
        char *m_url_real = (char *)malloc(sizeof(char) * 200);  // 分配内存
        strcpy(m_url_real, "/picture.html");  // 复制图片页面路径
        strncpy(m_real_file + len, m_url_real,
                strlen(m_url_real));  // 复制到实际文件路径
        free(m_url_real);             // 释放内存
    } else if (*(p + 1) == '6') {     // 如果 URL 以 /6 结尾
        char *m_url_real = (char *)malloc(sizeof(char) * 200);  // 分配内存
        strcpy(m_url_real, "/video.html");  // 复制视频页面路径
        strncpy(m_real_file + len, m_url_real,
                strlen(m_url_real));  // 复制到实际文件路径
        free(m_url_real);             // 释放内存
    } else if (*(p + 1) == '7') {     // 如果 URL 以 /7 结尾
        char *m_url_real = (char *)malloc(sizeof(char) * 200);  // 分配内存
        strcpy(m_url_real, "/fans.html");  // 复制粉丝页面路径
        strncpy(m_real_file + len, m_url_real,
                strlen(m_url_real));  // 复制到实际文件路径
        free(m_url_real);             // 释放内存
    } else
        strncpy(m_real_file + len, m_url,
                FILENAME_LEN - len - 1);  // 否则直接复制 URL 到实际文件路径

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;  // 获取文件状态，如果失败返回资源不存在

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;  // 如果文件不可读，返回禁止访问

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;  // 如果文件是目录，返回错误请求

    int fd = open(m_real_file, O_RDONLY);  // 打开文件
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ,
                                  MAP_PRIVATE, fd, 0);  // 将文件映射到内存
    close(fd);                                          // 关闭文件描述符
    return FILE_REQUEST;                                // 返回文件请求
}

// 解除内存映射
void http_conn::unmap() {
    if (m_file_address) {  // 如果文件地址不为空
        munmap(m_file_address, m_file_stat.st_size);  // 解除内存映射
        m_file_address = 0;  // 将文件地址置为空
    }
}

// 写入数据，用于将写缓冲区中的数据写入 socket
bool http_conn::write() {
    int temp = 0;

    if (bytes_to_send == 0) {  // 如果写入缓冲区完成，已经没有数据需要发送
        modfd(m_epollfd, m_sockfd, EPOLLIN,
              m_TRIGMode);  // 此时不再需要发送数据，而是需要监听读事件，以便读取客户端发送的数据
        init();             // 初始化连接
        return true;        // 返回写入成功
    }

    while (1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);  // 使用 writev 写入数据

        if (temp < 0) {             // temp变量是writev()的返回值，如果小于0，则写入失败
            if (errno == EAGAIN) {  // 如果是非阻塞模式下的 EAGAIN 错误
                modfd(m_epollfd, m_sockfd, EPOLLOUT,
                      m_TRIGMode);  // 修改 epoll 事件为写事件
                return true;        // 返回写入成功
            }
            unmap();       // 解除内存映射
            return false;  // 返回写入失败
        }

        bytes_have_send += temp;  // 更新已发送字节数
        bytes_to_send -= temp;    // 更新待发送字节数
        if (bytes_have_send >=
            m_iv[0].iov_len) {  // 如果已发送字节数大于等于第一个缓冲区的长度
            m_iv[0].iov_len = 0;  // 将第一个缓冲区的长度置为 0
            m_iv[1].iov_base =
                m_file_address +
                (bytes_have_send - m_write_idx);  // 设置第二个缓冲区的基地址
            m_iv[1].iov_len = bytes_to_send;  // 设置第二个缓冲区的长度
        } else {
            m_iv[0].iov_base =
                m_write_buf + bytes_have_send;  // 设置第一个缓冲区的基地址
            m_iv[0].iov_len =
                m_iv[0].iov_len - bytes_have_send;  // 设置第一个缓冲区的长度
        }

        if (bytes_to_send <= 0) {  // 如果写入缓冲区完成，已经没有数据需要发送
            unmap();               // 解除内存映射
            modfd(m_epollfd, m_sockfd, EPOLLIN,
                  m_TRIGMode);  // 此时不再需要发送数据，而是需要监听读事件，以便读取客户端发送的数据

            if (m_linger) {   // 如果需要保持连接
                init();       // 初始化连接
                return true;  // 返回写入成功
            } else {
                return false;  // 返回写入失败
            }
        }
    }
}

// 添加响应内容
bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;  // 如果写缓冲区已满，返回 false
    va_list arg_list;
    va_start(arg_list, format);  // 初始化可变参数列表
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx, format,
                        arg_list);  // 格式化字符串
    if (len >= (WRITE_BUFFER_SIZE - 1 -
                m_write_idx)) {  // 如果格式化后的字符串长度超过缓冲区剩余空间
        va_end(arg_list);  // 结束可变参数列表
        return false;      // 返回 false
    }
    m_write_idx += len;  // 更新写索引
    va_end(arg_list);    // 结束可变参数列表

    LOG_INFO("request:%s", m_write_buf);  // 记录请求内容

    return true;  // 返回 true
}

// 添加状态行
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
// 添加头部信息
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}
// 添加内容长度
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}
// 添加内容类型
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
// 添加连接状态
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n",
                        (m_linger == true) ? "keep-alive" : "close");
}
// 添加空行
bool http_conn::add_blank_line() { return add_response("%s", "\r\n"); }
// 添加内容
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

// 处理写入的 HTTP 响应，根据解析结果生成响应内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {                      // 如果是内部错误
            add_status_line(500, error_500_title);  // 添加状态行，状态码为 500
            add_headers(strlen(error_500_form));  // 添加头部信息
            if (!add_content(error_500_form)) return false;  // 添加错误信息内容
            break;
        }
        case BAD_REQUEST: {                         // 如果是错误请求
            add_status_line(404, error_404_title);  // 添加状态行，状态码为 404
            add_headers(strlen(error_404_form));  // 添加头部信息
            if (!add_content(error_404_form)) return false;  // 添加错误信息内容
            break;
        }
        case FORBIDDEN_REQUEST: {                   // 如果是禁止访问
            add_status_line(403, error_403_title);  // 添加状态行，状态码为 403
            add_headers(strlen(error_403_form));  // 添加头部信息
            if (!add_content(error_403_form)) return false;  // 添加错误信息内容
            break;
        }
        case FILE_REQUEST: {                     // 如果是文件请求
            add_status_line(200, ok_200_title);  // 添加状态行，状态码为 200
            if (m_file_stat.st_size != 0) {        // 如果文件大小不为 0
                add_headers(m_file_stat.st_size);  // 添加头部信息
                m_iv[0].iov_base = m_write_buf;  // 设置第一个缓冲区的基地址
                m_iv[0].iov_len = m_write_idx;  // 设置第一个缓冲区的长度
                m_iv[1].iov_base = m_file_address;  // 设置第二个缓冲区的基地址
                m_iv[1].iov_len =
                    m_file_stat.st_size;  // 设置第二个缓冲区的长度
                m_iv_count = 2;           // 设置缓冲区数量为 2
                bytes_to_send =
                    m_write_idx + m_file_stat.st_size;  // 设置待发送字节数
                return true;                            // 返回处理成功
            } else {
                const char *ok_string =
                    "<html><body></body></html>";  // 定义空页面内容
                add_headers(strlen(ok_string));    // 添加头部信息
                if (!add_content(ok_string)) return false;  // 添加空页面内容
            }
        }
        default:
            return false;  // 返回处理失败
    }
    m_iv[0].iov_base = m_write_buf;  // 设置第一个缓冲区的基地址
    m_iv[0].iov_len = m_write_idx;   // 设置第一个缓冲区的长度
    m_iv_count = 1;                  // 设置缓冲区数量为 1
    bytes_to_send = m_write_idx;     // 设置待发送字节数
    return true;                     // 返回处理成功
}

// 处理 HTTP 请求
void http_conn::process() {
    HTTP_CODE read_ret = process_read();  // 处理读取的 HTTP 请求
    if (read_ret == NO_REQUEST) {         // 如果没有请求
        modfd(m_epollfd, m_sockfd, EPOLLIN,
              m_TRIGMode);  // 修改 epoll 事件为读事件
        return;             // 返回
    }
    bool write_ret = process_write(read_ret);  // 处理写入的 HTTP 响应
    if (!write_ret) {                          // 如果写入失败
        close_conn();                          // 关闭连接
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT,
          m_TRIGMode);  // 修改 epoll 事件为写事件
}
