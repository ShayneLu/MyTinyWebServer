#include "config.h"

int main(int argc, char *argv[]) {
    // 需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    // 这里修改为我们设置的MySQL密码
    string passwd = "11111111";
    // 这里修改为我们设置的数据库名
    string databasename = "yourdb";

    // 命令行解析,启动时可以用户自己指定参数，Config类负责解析这些参数，缺省的参数用默认值
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;   // 创建WebServer对象

    // 初始化。将上面的config对象中的配置参数，传入到WebServer对象的init方法中，让WebServer对象也能使用
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite,
                config.OPT_LINGER, config.TRIGMode, config.sql_num,
                config.thread_num, config.close_log, config.actor_model);

    //  配置并初始化日志系统，用于记录服务器运行时的事件、错误、请求信息等 
    server.log_write();
    //  初始化数据库连接池，管理多个数据库连接，提高数据库访问效率。连接池可以避免频繁的数据库连接开销。
    server.sql_pool();
    // 初始化线程池，创建一定数量的工作线程，用于并发处理客户端请求，提升服务器的并发能力。
    server.thread_pool();
    //  设置触发模式，配置事件监听的触发方式（ LT 模式和 ET 模式），用于控制 I/O 多路复用的触发行为。
    server.trig_mode();
    //  监听端口，准备接受客户端的连接请求。
    server.eventListen();
    // 进入事件循环，开始处理客户端连接请求及其他事件。eventLoop 通常会运行在一个循环中，处理网络事件、请求解析、响应生成等操作。
    server.eventLoop();

    return 0;
}
