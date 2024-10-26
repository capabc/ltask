#ifndef ltask_config_h
#define ltask_config_h

#include <lua.h>

#define DEFAULT_MAX_SERVICE 65536
#define DEFAULT_QUEUE 4096
#define DEFAULT_QUEUE_SENDING DEFAULT_QUEUE
#define MAX_WORKER 256
#define MAX_SOCKEVENT 16

// 配置 Ltask 系统运行参数的结构体
struct ltask_config {
	// 表示工作线程的数量。工作线程用于调度和执行 Lua 服务，类似于并发环境中的工作线程池
	int worker;

	// 系统中的消息队列数量。每个队列可用于存储需要处理的消息，以便工作线程按需提取和处理。
	int queue;

	// 发送队列的大小或数量。该字段控制消息在发送过程中可以使用的队列数量，以便不同服务之间的消息传输更加高效
	int queue_sending;

	// 表示支持的最大服务数。Ltask 使用“服务”来指代独立的 Lua 虚拟机实例，
		// 每个实例都可以独立运行代码，因此 max_service 限制了可以并行运行的服务总数。
	int max_service;

	// 外部队列的数量，通常用于处理来自外部系统的任务或请求。这可以帮助系统与外部服务或应用集成
	int external_queue;

	// 存储崩溃日志文件的路径或名称（最大长度为 128 字节）。如果系统或某个 Lua 服务发生崩溃，日志将记录到该文件中，以便后续分析和调试。
	char crashlog[128];
};

void config_load(lua_State *L, int index, struct ltask_config *config);

#endif
