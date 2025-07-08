# 定义编译器变量，?= 表示如果未定义则使用g++，否则保留原值
CXX ?= g++

# 定义调试模式开关，1=调试模式，0=发布模式
DEBUG ?= 1

# 条件判断：如果是调试模式
ifeq ($(DEBUG), 1)
    # 添加调试符号 -g (gdb调试用)
    CXXFLAGS += -g
else
    # 否则添加优化选项 -O2 (发布模式优化)
    CXXFLAGS += -O2
endif

# 主构建目标：生成可执行文件server
# 冒号后列出所有依赖的源文件
server: main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  webserver.cpp config.cpp
#	# 编译命令：
#	# $(CXX) -o server       → 用定义的编译器生成server可执行文件
#	# $^                    → 自动展开所有依赖文件（即冒号后的文件列表）
#	# $(CXXFLAGS)           → 展开编译选项（-g或-O2）
#	# -lpthread -lmysqlclient → 链接pthread和mysql库
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

# 清理目标
# 	删除生成的server可执行文件
# 	-r 参数防止删除目录时报错（虽然这里server是文件不是目录）
clean:
	rm  -r server