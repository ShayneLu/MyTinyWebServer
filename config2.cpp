#include "config.h"

// Config类的构造函数
Config::Config() {
    // 端口号，默认设置为9006
    PORT = 9006;

    // 日志写入方式，0表示同步写入（默认），1表示异步写入
    LOGWrite = 0;

    // 触发组合模式，0表示默认模式（listenfd LT + connfd LT）
    TRIGMode = 0;

    // listenfd的触发模式，0表示LT（水平触发，默认）
    LISTENTrigmode = 0;

    // connfd的触发模式，0表示LT（水平触发，默认）
    CONNTrigmode = 0;

    // 是否启用优雅关闭连接，0表示不启用（默认）
    OPT_LINGER = 0;

    // 数据库连接池数量，默认设置为8
    sql_num = 8;

    // 线程池内的线程数量，默认设置为8
    thread_num = 8;

    // 是否关闭日志功能，0表示不关闭（默认），1表示关闭
    close_log = 0;

    // 并发模型选择，0表示Proactor模式（默认），1表示Reactor模式
    actor_model = 0;
}

// 解析命令行参数的函数
void Config::parse_arg(int argc, char *argv[]) {
    int opt;  // 用于存储当前解析到的选项字符
    // 定义支持的选项字符串，冒号表示该选项需要一个附加参数值
    // 例如：-p 8080 中的8080是附加参数值
    const char *str = "p:l:m:o:s:t:c:a:";

// | -p | 整数 | PORT | 设置服务器的端口号（默认 9006） |
// | -l | 整数 | LOGWrite | 设置日志写入方式（0 为同步，1 为异步） |
// | -m | 整数 | TRIGMode | 设置connfd的触发模式（如 LT 或 ET 模式） |
// | -o | 整数 | OPT_LINGER | 设置是否优雅关闭连接（0 为禁用，1 为启用） |
// | -s | 整数 | sql_num | 设置数据库连接池的数量（默认 8） |
// | -t | 整数 | thread_num | 设置线程池的线程数量（默认 8） |
// | -c | 整数 | close_log | 设置是否关闭日志（0 为不关闭，1 为关闭） |
// | -a | 整数 | actor_model | 设置并发模型（0 为 Proactor，1 为 Reactor） |

    // 使用getopt函数逐个解析命令行参数
    // getopt会返回当前解析到的选项字符（如p、l等），如果选项需要参数值，则通过optarg获取
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
            case 'p': {               // 解析端口号参数
                PORT = atoi(optarg);  // 将字符串转换为整数并赋值给PORT
                break;
            }
            case 'l': {                   // 解析日志写入方式参数
                LOGWrite = atoi(optarg);  // 赋值给LOGWrite
                break;
            }
            case 'm': {                   // 解析触发组合模式参数
                TRIGMode = atoi(optarg);  // 赋值给TRIGMode
                break;
            }
            case 'o': {                     // 解析优雅关闭连接参数
                OPT_LINGER = atoi(optarg);  // 赋值给OPT_LINGER
                break;
            }
            case 's': {                  // 解析数据库连接池数量参数
                sql_num = atoi(optarg);  // 赋值给sql_num
                break;
            }
            case 't': {                     // 解析线程池线程数量参数
                thread_num = atoi(optarg);  // 赋值给thread_num
                break;
            }
            case 'c': {                    // 解析是否关闭日志参数
                close_log = atoi(optarg);  // 赋值给close_log
                break;
            }
            case 'a': {                      // 解析并发模型参数
                actor_model = atoi(optarg);  // 赋值给actor_model
                break;
            }
            default:  // 如果遇到未定义的选项，跳过
                break;
        }
    }
}