#include "service.h"
#include "queue.h"
#include "config.h"
#include "message.h"
#include "systime.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

// test whether an unsigned value is a power of 2 (or zero)
#define ispow2(x)	(((x) & ((x) - 1)) == 0)

#define TYPEID_STRING 0
#define TYPEID_TABLE 1
#define TYPEID_FUNCTION 2
#define TYPEID_USERDATA 3
#define TYPEID_THREAD 4
#define TYPEID_NONEOBJECT 5
#define TYPEID_COUNT 6

static int
lua_typeid[LUA_NUMTYPES] = {
	TYPEID_NONEOBJECT,	// LUA_TNIL
	TYPEID_NONEOBJECT,	// LUA_TBOOLEAN
	TYPEID_NONEOBJECT,	// LUA_TLIGHTUSERDATA
	TYPEID_NONEOBJECT,	// LUA_TNUMBER
	TYPEID_STRING,
	TYPEID_TABLE,
	TYPEID_FUNCTION,
	TYPEID_USERDATA,
	TYPEID_THREAD,
};

struct memory_stat {
	size_t count[TYPEID_COUNT];
	size_t mem;
	size_t limit;
};

// struct service 确保了 Ltask 系统能够灵活地管理多个服务实例，通过有效的消息传递和状态管理，支持高效的任务调度。
struct service {
	// 指向当前 lua_State的指针。每个服务在执行其逻辑时会使用此lua_State来运行 Lua 代码.
	lua_State *L;
	/*
		lua_State 是 Lua 编程语言的核心数据结构，用于表示一个独立的 Lua 解释器状态。
		每个 lua_State 实例封装了一个 Lua 虚拟机，包括其堆栈、全局变量和其他运行时信息。这使得 Lua 能够在多线程或多协程环境中安全地运行多个独立的脚本
		主要特点
			1. 状态管理：每个 lua_State 保存了独立的 Lua 状态，包括栈、全局环境等，允许多个 Lua 代码片段并行运行而不会相互干扰。

			2. 内存管理：Lua 使用自动内存管理（垃圾回收），lua_State 结构包含指向 Lua 内部内存管理系统的数据，使得资源的分配和释放更加高效。
			
			3. C API 交互：通过 lua_State，C/C++ 程序可以与 Lua 脚本进行交互。Lua 提供了一套 C API，允许开发者推入和弹出栈上的数据，以便在 C 代码和 Lua 之间传递信息。
			
			4. 协程支持：lua_State 也支持协程，使得程序可以在单线程中实现异步和非阻塞的操作。

		示例用途
			1. 在游戏开发中，lua_State 可以用来创建独立的游戏脚本实例，每个游戏对象可能都有自己的脚本状态。
			2. 在服务器应用中，可以为每个客户端请求分配一个 lua_State，以便处理各自的逻辑。

		更多内容见：
			https://www.lua.org/manual/5.1/
		
		以下是官网描述：
		lua_State  typedef struct lua_State lua_State;
		
		Opaque structure that keeps the whole state of a Lua interpreter. 
		lua_State 是一个封装了整个 Lua 解释器状态的结构体。

		The Lua library is fully reentrant: it has no global variables. All information about a state is kept in this structure.
		Lua 库是完全可重入的：它没有全局变量。与状态相关的所有信息都保存在这个结构体中。

		A pointer to this state must be passed as the first argument to every function in the library, except to lua_newstate, which creates a Lua state from scratch.
		// 在调用库中的每个函数时，必须将指向这个状态的指针作为第一个参数传递，除了 lua_newstate 函数，它用于从头创建一个新的 Lua 状态。
	*/

	// 指向另一个 lua_State的指针。可能用于支持协程或并行任务处理，允许服务异步执行
	lua_State *rL;

	// 指向消息队列的指针。服务通过此队列接收来自其他服务或外部系统的消息。
	struct queue *msg;

	// 指向输出消息的指针。用于存储当前服务准备发送的消息。
	struct message *out;

	// 指向反弹消息的指针。用于处理需要返回给发送者的消息，通常在服务出错时使用。 
	struct message *bounce;

	// 服务的当前状态，通常包括运行中、等待、阻塞或已停止等状态，用于调度和管理服务。
	int status;

	// 消息接收计数器，跟踪该服务接收到的消息数量，便于统计和监控。
	int receipt;

	// 表示绑定到此服务的工作线程的 ID，帮助调度器了解哪个线程在处理该服务的请求。
	int binding_thread;

	// 用于sockevent的 ID，通常与事件通知系统关联，帮助处理网络事件。
	int sockevent_id;

	// 服务的唯一标识符，这个 ID 用于在系统中唯一标识每个服务实例，确保管理的精确性。 
	service_id id;

	// 服务的标签，用于给服务一个可读的名称或描述，便于调试和日志记录。
	char label[32];

	// 内存统计信息，记录该服务使用的内存数据，以帮助性能分析和资源管理。
	struct memory_stat stat;

	// 记录服务消耗的 CPU 时间，通常用于性能监控，帮助评估服务效率。
	uint64_t cpucost;

	// 记录服务的时钟时间戳，用于处理超时或调度逻辑，帮助调度器决定服务的运行顺序。
	uint64_t clock;
};


// struct service_pool 的设计允许 Ltask 系统有效地管理和调度多个服务实例，为系统的扩展性和灵活性提供了支持
struct service_pool {
	// 这个字段可能用于位掩码操作，可以帮助快速过滤服务的状态或特性。例如，它可以用来标识哪些服务是可用的、正在运行的，或者其他状态信息。
	int mask;

	// 表示当前服务池中服务的数量。此字段对于监控服务的数量和动态调整资源非常重要。
	int queue_length;

	// 服务池的唯一标识符。这个 ID 使得不同的服务池可以被区分和管理，确保在系统中不会出现冲突.
	unsigned int id;

	// 这是一个指向服务指针的指针，实际上是一个动态数组，存储了所有服务的指针。通过这个结构，服务可以动态添加或移除，灵活地管理服务的生命周期。
	struct service **s;
};


struct service_pool *
service_create(struct ltask_config *config) {
	struct service_pool tmp;
	assert(ispow2(config->max_service));
	tmp.mask = config->max_service - 1;
	tmp.id = 0;
	tmp.queue_length = config->queue;
	tmp.s = (struct service **)malloc(sizeof(struct service *) * config->max_service);
	if (tmp.s == NULL)
		return NULL;
	struct service_pool * r = (struct service_pool *)malloc(sizeof(tmp));
	*r = tmp;
	int i;
	for (i=0;i<config->max_service;i++) {
		r->s[i] = NULL;
	}
	return r;
}

static void
free_service(struct service *S) {
	if (S->L != NULL)
		lua_close(S->L);
	if (S->msg) {
		for (;;) {
			struct message *m = queue_pop_ptr(S->msg);
			if (m) {
				message_delete(m);
			} else {
				break;
			}
		}
		queue_delete(S->msg);
	}
	message_delete(S->out);
	message_delete(S->bounce);
	S->receipt = MESSAGE_RECEIPT_NONE;
}

void
service_destory(struct service_pool *p) {
	if (p == NULL)
		return;
	int i;
	for (i=0;i<=p->mask;i++) {
		struct service *s = p->s[i];
		if (s) {
			free_service(s);
		}
	}
	free(p->s);
	free(p);
}

static void
init_service_key(lua_State *L, void *ud, size_t sz) {
	lua_pushlstring(L, (const char *)ud, sz);
	lua_setfield(L, LUA_REGISTRYINDEX, LTASK_KEY);
}

static int
init_service(lua_State *L) {
	void *ud = lua_touserdata(L, 1);
	size_t sz = lua_tointeger(L, 2);
	init_service_key(L, ud, sz);
	luaL_openlibs(L);
	lua_gc(L, LUA_GCGEN, 0, 0);
	return 0;
}

static inline struct service **
service_slot(struct service_pool *p, unsigned int id) {
	return &p->s[id & p->mask];
}

service_id
service_new(struct service_pool *p, unsigned int sid) {
	service_id result = { 0 };
	unsigned int id;
	if (sid != 0) {
		id = sid;
		if (*service_slot(p, id) != NULL) {
			return result;
		}
	} else {
		id = p->id;
		int i = 0;
		while (id == 0 || *service_slot(p, id) != NULL) {
			++id;
			if (++i > p->mask) {
				return result;
			}
		}
		p->id = id + 1;
	}
	struct service *s = (struct service *)malloc(sizeof(*s));
	if (s == NULL)
		return result;
	s->L = NULL;
	s->rL = NULL;
	s->msg = NULL;
	s->out = NULL;
	s->bounce = NULL;
	s->receipt = MESSAGE_RECEIPT_NONE;
	s->id.id = id;
	s->status = SERVICE_STATUS_UNINITIALIZED;
	s->binding_thread = -1;
	s->sockevent_id = -1;
	s->cpucost = 0;
	s->clock = 0;
	*service_slot(p, id) = s;
	result.id = id;
	return result;
}

static inline struct service *
get_service(struct service_pool *p, service_id id) {
	struct service *S = *service_slot(p, id.id);
	if (S == NULL || S->id.id != id.id)
		return NULL;
	return S;
}

static inline int
check_limit(struct memory_stat *stat) {
	if (stat->limit == 0)
		return 0;
	return (stat->mem > stat->limit);
}

static void *
service_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	struct memory_stat *stat = (struct memory_stat *)ud;
	if (nsize == 0) {
		stat->mem -= osize;
		free(ptr);
		return NULL;
	} else if (ptr == NULL) {
		// new object
		if (check_limit(stat)) {
			return NULL;
		}
		if (osize >=0 && osize < LUA_NUMTYPES) {
			int id = lua_typeid[osize];
			stat->count[id]++;
		}
		void * ret = malloc(nsize);
		if (ret == NULL) {
			return NULL;
		}
		stat->mem += nsize;
		return ret;
	} else {
		if (osize > nsize && check_limit(stat)) {
			return NULL;
		}
		void * ret = realloc(ptr, nsize);
		if (ret == NULL)
			return NULL;
		stat->mem += nsize;
		stat->mem -= osize;
		return ret;
	}
}

static int
pushstring(lua_State *L) {
	const char * msg = (const char *)lua_touserdata(L, 1);
	lua_settop(L, 1);
	lua_pushstring(L, msg);
	return 1;
}

static void
error_message(lua_State *fromL, lua_State *toL, const char *msg) {
	if (toL == NULL)
		return;
	if (fromL == NULL) {
		lua_pushlightuserdata(toL, (void *)msg);
	} else {
		const char * err = lua_tostring(fromL, -1);
		lua_pushcfunction(toL, pushstring);
		lua_pushlightuserdata(toL, (void *)err);
		if (lua_pcall(toL, 1, 1, 0) != LUA_OK) {
			lua_pop(toL, 1);
			lua_pushlightuserdata(toL, (void *)msg);
		}
	}
}

int
service_init(struct service_pool *p, service_id id, void *ud, size_t sz, void *pL) {
	struct service *S = get_service(p, id);
	assert(S != NULL && S->L == NULL && S->status == SERVICE_STATUS_UNINITIALIZED);
	lua_State *L;
	memset(&S->stat, 0, sizeof(S->stat));
	L = lua_newstate(service_alloc, &S->stat);
	if (L == NULL)
		return 1;
	lua_pushcfunction(L, init_service);
	lua_pushlightuserdata(L, ud);
	lua_pushinteger(L, sz);
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		error_message(L, pL, "Init lua state error");
		lua_close(L);
		return 1;
	}
	S->msg = queue_new_ptr(p->queue_length);
	if (S->msg == NULL) {
		error_message(NULL, pL, "New queue error");
		lua_close(L);
		return 1;
	}
	S->L = L;
	S->rL = lua_newthread(L);
	luaL_ref(L, LUA_REGISTRYINDEX);

	return 0;
}

size_t
service_memlimit(struct service_pool *p, service_id id, size_t limit) {
	struct service *S = get_service(p, id);
	if (S == NULL || S->L == NULL)
		return (size_t)-1;
	size_t ret = S->stat.limit;
	S->stat.limit = limit;
	return ret;
}

size_t
service_memcount(struct service_pool *p, service_id id, int luatype) {
	struct service *S = get_service(p, id);
	assert(luatype >=0 && luatype < LUA_NUMTYPES);
	if (S == NULL || S->L == NULL)
		return (size_t)-1;
	int type = lua_typeid[luatype];
	return S->stat.count[type];
}

static int
require_cmodule(lua_State *L) {
	const char *name = (const char *)lua_touserdata(L, 1);
	lua_CFunction f = (lua_CFunction)lua_touserdata(L, 2);
	luaL_requiref(L, name, f, 0);
	return 0;
}

int
service_requiref(struct service_pool *p, service_id id, const char *name, void *f, void *pL) {
	struct service *S = get_service(p, id);
	if (S == NULL || S->rL == NULL) {
		error_message(NULL, pL, "requiref : No service");
		return 1;
	}
	lua_State *L = S->rL;
	lua_pushcfunction(L, require_cmodule);
	lua_pushlightuserdata(L, (void *)name);
	lua_pushlightuserdata(L, f);
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		error_message(L, pL, "requiref : pcall error");
		lua_pop(L, 1);
		return 1;
	}
	return 0;
}

int
service_setlabel(struct service_pool *p, service_id id, const char *label) {
	struct service *S = get_service(p, id);
	if (S == NULL)
		return 1;
	strncpy(S->label, label, sizeof(S->label)-1);
	S->label[sizeof(S->label)-1] = '\0';
	return 0;
}

const char *
service_getlabel(struct service_pool *p, service_id id) {
	struct service *S = get_service(p, id);
	if (S == NULL)
		return "<dead service>";
	return S->label;
}

void 
service_close(struct service_pool *p, service_id id) {
	struct service * s = get_service(p, id);
	if (s) {
		if (s->L) {
			lua_close(s->L);
			s->L = NULL;
		}
		s->status = SERVICE_STATUS_DEAD;
	}
}

void
service_delete(struct service_pool *p, service_id id) {
	struct service * s = get_service(p, id);
	if (s) {
		*service_slot(p, id.id) = NULL;
		free_service(s);
	}
}

const char *
service_loadstring(struct service_pool *p, service_id id, const char *source, size_t source_sz, const char *chunkname) {
	struct service *S= get_service(p, id);
	if (S == NULL || S->L == NULL)
		return "Init service first";
	lua_State *L = S->L;
	if (luaL_loadbuffer(L, source, source_sz, chunkname) != LUA_OK) {
		const char * r = lua_tostring(S->L, -1);
		S->status = SERVICE_STATUS_DEAD;
		return r;
	}
	S->status = SERVICE_STATUS_IDLE;
	return NULL;
}

int
service_resume(struct service_pool *p, service_id id) {
	struct service *S= get_service(p, id);
	if (S == NULL)
		return 1;
	lua_State *L = S->L;
	if (L == NULL)
		return 1;
	int nresults = 0;
	uint64_t start = systime_thread();
	S->clock = start;
	int r = lua_resume(L, NULL, 0, &nresults);
	uint64_t end = systime_thread();
	S->cpucost += end - start;
	if (r == LUA_YIELD) {
		lua_pop(L, nresults);
		return 0;
	}
	if (r == LUA_OK) {
		return 1;
	}
	if (!lua_checkstack(L, LUA_MINSTACK)) {
		lua_writestringerror("%s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
		return 1;
	}
	lua_pushfstring(L, "Service %d error: %s", id.id, lua_tostring(L, -1));
	luaL_traceback(L, L, lua_tostring(L, -1), 0);
	lua_writestringerror("%s\n", lua_tostring(L, -1));
	lua_pop(L, 3);
	return 1;
}

int
service_push_message(struct service_pool *p, service_id id, struct message *msg) {
	struct service *s = get_service(p, id);
	if (s == NULL || s->status == SERVICE_STATUS_DEAD)
		return -1;
	if (queue_push_ptr(s->msg, msg)) {
		// blocked
		return 1;
	}
	return 0;
}

int
service_status_get(struct service_pool *p, service_id id) {
	struct service *s = get_service(p, id);
	if (s == NULL)
		return SERVICE_STATUS_DEAD;
	return s->status;	
}

void
service_status_set(struct service_pool *p, service_id id, int status) {
	struct service *s = get_service(p, id);
	if (s == NULL)
		return;
	s->status = status;
}

struct message *
service_message_out(struct service_pool *p, service_id id) {
	struct service *s = get_service(p, id);
	if (s == NULL)
		return NULL;
	struct message * r = s->out;
	if (r)
		s->out = NULL;
	return r;
}

int
service_send_message(struct service_pool *p, service_id id, struct message *msg) {
	struct service *s = get_service(p, id);
	if (s == NULL || s->out != NULL)
		return 1;
	s->out = msg;
	return 0;
}

void
service_write_receipt(struct service_pool *p, service_id id, int receipt, struct message *bounce) {
	struct service *s = get_service(p, id);
	if (s != NULL && s->receipt == MESSAGE_RECEIPT_NONE) {
		s->receipt = receipt;
		s->bounce = bounce;
	} else {
		fprintf(stderr, "WARNING: write receipt %d fail (%d)\n", id.id, s->receipt);
		if (s) {
			message_delete(s->bounce);
			s->receipt = receipt;
			s->bounce = bounce;
		}
	}
}

struct message *
service_read_receipt(struct service_pool *p, service_id id, int *receipt) {
	struct service *s = get_service(p, id);
	if (s == NULL) {
		*receipt = MESSAGE_RECEIPT_NONE;
		return NULL;
	}
	*receipt = s->receipt;
	struct message *r = s->bounce;
	s->receipt = MESSAGE_RECEIPT_NONE;
	s->bounce = NULL;
	return r;
}

struct message *
service_pop_message(struct service_pool *p, service_id id) {
	struct service *s = get_service(p, id);
	if (s == NULL)
		return NULL;
	if (s->bounce) {
		struct message *r = s->bounce;
		s->bounce = NULL;
		return r;
	}
	return queue_pop_ptr(s->msg);
}

int
service_has_message(struct service_pool *p, service_id id) {
	struct service *s = get_service(p, id);
	if (s == NULL)
		return 0;
	if (s->receipt != MESSAGE_RECEIPT_NONE) {
		return 1;
	}
	return queue_length(s->msg) > 0;
}

void
service_send_signal(struct service_pool *p, service_id id) {
	struct service *s = get_service(p, id);
	if (s == NULL)
		return;
	if (s->out)
		message_delete(s->out);
	struct message msg;
	msg.from = id;
	msg.to.id = SERVICE_ID_ROOT;
	msg.session = 0;
	msg.type = MESSAGE_SIGNAL;
	msg.msg = NULL;
	msg.sz = 0;

	s->out = message_new(&msg);
}

struct strbuff {
	char *buf;
	size_t sz;
};

static size_t
addstr(struct strbuff *b, const char *str, size_t sz) {
	if (b->sz < sz) {
		size_t n = b->sz - 1;
		memcpy(b->buf, str, n);
		b->buf[n] = 0;
		b->buf += n;
		b->sz = 0;
	} else {
		memcpy(b->buf, str, sz);
		b->buf += sz;
		b->sz -= sz;
	}
	return b->sz;
}

#define addliteral(b, s) addstr(b, s, sizeof(s "") -1)

static size_t
addfuncname(lua_Debug *ar, struct strbuff *b) {
	if (*ar->namewhat != '\0')  {	/* is there a name from code? */
		char name[1024];
		int n = snprintf(name, sizeof(name), "%s '%s'", ar->namewhat, ar->name);  /* use it */
		return addstr(b, name, n);
	} else if (*ar->what == 'm') {  /* main? */
		return addliteral(b, "main chunk");
	} else if (*ar->what != 'C') { /* for Lua functions, use <file:line> */
		char name[1024];
		int n = snprintf(name, sizeof(name), "function <%s:%d>", ar->short_src, ar->linedefined);
		return addstr(b, name, n);
	} else { /* nothing left... */
		return addliteral(b, "?");
	}
}

static lua_State *
find_running(lua_State *L) {
	int level = 0;
	lua_Debug ar;
	while (lua_getstack(L, level++, &ar)) {
		lua_getinfo(L, "u", &ar);
		if (ar.nparams > 0) {
			lua_getlocal(L, &ar, 1);
			if (lua_type(L, -1) == LUA_TTHREAD) {
				lua_State *co = lua_tothread(L, -1);
				lua_pop(L, 1);
				return co;
			} else {
				lua_pop(L, 1);
			}
		}
	}
	return L;
}

int
service_backtrace(struct service_pool *p, service_id id, char *buf, size_t sz) {
	struct service *S= get_service(p, id);
	if (S == NULL)
		return 0;
	lua_State *L = find_running(S->L);
	struct strbuff b = { buf, sz };
	int level = 0;
	lua_Debug ar;
	char line[1024];
	while (lua_getstack(L, level++, &ar)) {
		lua_getinfo(L, "Slnt", &ar);
		int n;
		if (ar.currentline <= 0) {
			n = snprintf(line, sizeof(line), "%s: in ", ar.short_src);
		} else {
			n = snprintf(line, sizeof(line), "%s:%d: in ", ar.short_src, ar.currentline);
		}
		if (addstr(&b, line, n) == 0) {
			return sz;
		}
		if (addfuncname(&ar, &b) == 0) {
			return sz;
		}
		if (addliteral(&b, "\n") == 0) {
			return sz;
		}
		if (ar.istailcall) {
			if (addliteral(&b, "(...tail calls...)\n") == 0) {
				return sz;
			}
		}
	}
	return (int)(sz - b.sz);
}

uint64_t
service_cpucost(struct service_pool *p, service_id id) {
	struct service *S= get_service(p, id);
	if (S == NULL)
		return 0;
	return S->cpucost + systime_thread() - S->clock;
}

int
service_binding_get(struct service_pool *p, service_id id) {
	struct service *S= get_service(p, id);
	if (S == NULL)
		return -1;
	return S->binding_thread;
}

void
service_binding_set(struct service_pool *p, service_id id, int worker_thread) {
	struct service *S= get_service(p, id);
	if (S == NULL)
		return;
	S->binding_thread = worker_thread;
}

int
service_sockevent_get(struct service_pool *p, service_id id) {
	struct service *S= get_service(p, id);
	if (S == NULL)
		return -1;
	return S->sockevent_id;
}

void
service_sockevent_init(struct service_pool *p, service_id id, int index) {
	struct service *S= get_service(p, id);
	if (S == NULL)
		return;
	S->sockevent_id = index;
}
