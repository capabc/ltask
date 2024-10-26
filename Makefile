# CFLAGS 是用于指定编译器选项的变量, 
  # -g：生成调试信息，使得程序在调试器中可用  
  # -Wall：开启所有警告，帮助开发者发现潜在的问题
CFLAGS=-g -Wall

# 注释掉的，如果启用，将会添加一个宏定义 DEBUGLOG，在编译时可用于条件编译。
# CFLAGS+=-DDEBUGLOG

# LUAINC?= 是一个在 Makefile 中使用的语法，它用于设置一个变量的值，只有在这个变量尚未被定义的情况下才会赋值
  # `pkgconf lua --cflags`
  # 这是一个反引号（grave accent）包围的命令替换语法，用于在 Makefile 中执行 shell 命令，并将输出结果赋值给变量。
  # pkgconf lua --cflags 是一个命令，它使用 pkgconf 工具查询与 lua 相关的编译标志。

    # 这个命令的输出通常包括编译时需要包含的头文件路径和其他相关的编译标志。
LUAINC?=`pkgconf lua --cflags`

# 操作系统判断， 根据不同的操作系统设置不同的编译选项和库：
ifeq ($(OS),Windows_NT) # Windows
  # LIBS 包含多个库的链接选项，包含 Windows 特有的库，ws2_32（Windows 套接字库）和 winmm（Windows 多媒体库）库。
    # -D ：这是 GCC 和其他编译器的命令行选项，用于定义一个预处理器宏。定义后，您可以在代码中使用这个宏。
    # _WIN32_WINNT ：这是一个特定于 Windows 的宏，表示您希望编译的 Windows API 的最小版本。根据
    # 0x0601 ：这是一个十六进制数，表示 Windows 10 的版本号。具体来说，0x0601 对应的是 Windows 10（即 Windows 10 的内部版本号为 10.0）。如果您使用其他版本的 Windows，您可以通过更改这个值来指定不同的版本：
        # 0x0501：对应 Windows XP
        # 0x0600：对应 Windows Vista
        # 0x0602：对应 Windows 8
        # 0x0603：对应 Windows 8.1
  LIBS=-lwinmm -lws2_32 -D_WIN32_WINNT=0x0601 -lntdll
  # SHARED 指定共享库的链接选项
  SHARED=--shared
  # SO 设置为 dll，表示生成的动态库文件扩展名
  SO=dll
  #LUALIB 设置 Lua 库的搜索路径和库名
  LUALIB?=`pkgconf lua --libs`
else ifeq ($(OS), Darwin)  # macOS (Darwin)
  # SO 设置为 so，表示生成的动态库文件扩展名
  SO=so
  # SHARED 设置为 macOS 的动态库链接选项。
  SHARED= -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
else # 其他 Unix 系统
  # SHARED 设置为共享库链接选项
  SHARED=--shared -fPIC
  # SO 设置为 so，表示生成的动态库文件扩展名。
  SO=so
  # LIBS 包含 pthread 库。
  LIBS=-lpthread
endif

# 构建目标
# all 是默认目标，执行时会构建 ltask.$(SO)（根据操作系统生成的扩展名）
all : ltask.$(SO)

# 源文件
SRCS=\
 src/ltask.c \
 src/queue.c \
 src/sysinfo.c \
 src/service.c \
 src/config.c \
 src/lua-seri.c \
 src/message.c \
 src/systime.c \
 src/timer.c \
 src/sysapi.c \
 src/logqueue.c \
 src/debuglog.c \
 src/threadsig.c

# 生成规则
# ltask.$(SO) 目标依赖于 SRCS 中的所有源文件，使用 $@ 代表目标文件名，$^ 代表所有依赖文件。
# 使用 $(CC)（编译器）和之前定义的选项编译源文件。
ltask.$(SO) : $(SRCS)
	$(CC) $(CFLAGS) $(SHARED) $(LUAINC) -Isrc -o $@ $^ $(LUALIB) $(LIBS)

# seri.$(SO) 目标仅依赖于 src/lua-seri.c，使用 -D TEST_SERI 进行条件编译。
seri.$(SO) : src/lua-seri.c
	$(CC) $(CFLAGS) $(SHARED) $(LUAINC) -Isrc -o $@ $^ $(LUALIB) -D TEST_SERI

# 清理规则
clean :
	rm -rf *.$(SO)


