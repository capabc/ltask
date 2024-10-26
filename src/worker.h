#ifndef ltask_worker_h
#define ltask_worker_h

#include <stdint.h>

#include "atomic.h"
#include "thread.h"
#include "service.h"
#include "debuglog.h"
#include "cond.h"
#include "systime.h"

struct ltask;

#define BINDING_SERVICE_QUEUE 16

// 在 Ltask 系统中，struct binding_service 结构体用于实现一个服务的绑定队列。
// 此结构体提供了一个固定大小的循环队列，用于保存需要绑定的服务，以便它们可以有序地分配给线程进行处理。
struct binding_service {
	// 指向队列的头部位置，表示下一个将要处理的服务在队列中的位置。
	int head;

	// 指向队列的尾部位置，表示新加入队列的服务位置。队列采用环形结构，因此 tail 位置会在到达队列尾部后回到头部，实现循环使用。
	int tail;

	// 一个固定大小的数组，用于存储队列中的服务, BINDING_SERVICE_QUEUE，默认为16.
	service_id q[BINDING_SERVICE_QUEUE];
};

// worker_thread 结构体用于定义 Ltask 系统中的工作线程
struct worker_thread {
	// 指向 ltask 的指针，用于关联线程与其所管理的任务系统
	struct ltask *task;

	// （在 DEBUGLOG 定义下可用）：指向调试日志记录器 debug_logger 的指针，仅在启用调试模式下存在。用于记录线程调试信息
#ifdef DEBUGLOG
	struct debug_logger *logger;
#endif

	// 线程的唯一标识符，用于标识线程 ID
	int worker_id;

	// 这三个字段表示当前线程中运行的服务、绑定的服务和等待的服务状态。它们共同协作控制线程的任务处理状态
	service_id running;
	service_id binding;
	service_id waiting;

	// 原子类型的整数，分别用于标识服务是否就绪和完成。原子操作确保在多线程环境下的线程安全。
	atomic_int service_ready;
	atomic_int service_done;

	// 线程的终止信号标志，通常用于指示线程是否需要结束
	int term_signal;

	// 用于标识线程的睡眠和唤醒状态。这些状态可帮助管理线程的调度，例如在没有任务时进入休眠，待有任务时再唤醒。
	int sleeping;
	int wakeup;

	// 标记线程是否处于忙碌状态，用于在调度时确认线程的当前工作负荷
	int busy;

	// 条件变量 cond，用于线程的同步控制，可以阻塞或唤醒线程
	struct cond trigger;

	// binding_service 类型的队列，用于存储绑定的服务任务，确保服务能有序地被线程处理。
	struct binding_service binding_queue;

	// 线程的调度时间戳，类型为 uint64_t，记录最后一次调度的时间，用于控制调度频率或性能分析。
	uint64_t schedule_time;
};

static inline void
worker_init(struct worker_thread *worker, struct ltask *task, int worker_id) {
	worker->task = task;
#ifdef DEBUGLOG
	worker->logger = dlog_new("WORKER", worker_id);
#endif
	worker->worker_id = worker_id;
	atomic_int_init(&worker->service_ready, 0);
	atomic_int_init(&worker->service_done, 0);
	cond_create(&worker->trigger);
	worker->running.id = 0;
	worker->binding.id = 0;
	worker->waiting.id = 0;
	worker->term_signal = 0;
	worker->sleeping = 0;
	worker->wakeup = 0;
	worker->busy = 0;
	worker->binding_queue.head = 0;
	worker->binding_queue.tail = 0;
}

static inline int
worker_has_job(struct worker_thread *worker) {
	return worker->service_ready != 0;
}

static inline void
worker_sleep(struct worker_thread *w) {
	if (w->term_signal)
		return;
	cond_wait_begin(&w->trigger);
	if (worker_has_job(w)) {
		w->wakeup = 0;
	} else {
		if (w->wakeup) {
			w->wakeup = 0;
		} else {
			w->sleeping = 1;
			cond_wait(&w->trigger);
			w->sleeping = 0;
		}
	}
	cond_wait_end(&w->trigger);
}

static inline int
worker_wakeup(struct worker_thread *w) {
	int sleeping;
	cond_trigger_begin(&w->trigger);
	sleeping = w->sleeping;
	w->wakeup = 1;
	cond_trigger_end(&w->trigger, sleeping);
	return sleeping;
}

static inline void
worker_quit(struct worker_thread *w) {
	cond_trigger_begin(&w->trigger);
	w->sleeping = 0;
	cond_trigger_end(&w->trigger, 0);
}

static inline void
worker_destory(struct worker_thread *worker) {
	cond_release(&worker->trigger);
}

// Calling by Scheduler. 0 : succ
static inline int
worker_binding_job(struct worker_thread *worker, service_id id) {
	struct binding_service * q = &(worker->binding_queue);
	if (q->tail - q->head >= BINDING_SERVICE_QUEUE)	// queue full
		return 1;
	q->q[q->tail % BINDING_SERVICE_QUEUE] = id;
	++q->tail;
	assert(q->tail > 0);
	return 0;
}

// Calling by Scheduler, may produce service_ready. 0 : succ
static inline service_id
worker_assign_job(struct worker_thread *worker, service_id id) {
	if (worker->service_ready == 0) {
		// try binding queue itself
		struct binding_service * q = &(worker->binding_queue);
		if (q->tail != q->head) {
			id = q->q[q->head % BINDING_SERVICE_QUEUE];
			++q->head;
			if (q->head == q->tail)
				q->head = q->tail = 0;
		}
		// only one producer (Woker) except itself (worker_steal_job), so don't need use CAS to set
		worker->service_ready = id.id;
		return id;
	} else {
		// Already has a job
		service_id ret = { 0 };
		return ret;
	}
}

// Calling by Worker, may consume service_ready
static inline service_id
worker_get_job(struct worker_thread *worker) {
	service_id id = { 0 };
	for (;;) {
		int job = worker->service_ready;
		if (job) {
			if (atomic_int_cas(&worker->service_ready, job, 0)) {
				id.id = job;
				break;
			}
		} else {
			break;
		}
	}
	return id;
}

// Calling by Scheduler, may consume service_ready
static inline service_id
worker_steal_job(struct worker_thread *worker, struct service_pool *p) {
	service_id id = { 0 };
	int job = worker->service_ready;
	if (job) {
		service_id t = { job };
		int worker_id = service_binding_get(p, t);
		if (worker_id == worker->worker_id) {
			// binding job, can't steal
			return id;
		}
		if (atomic_int_cas(&worker->service_ready, job, 0)) {
			id = t;
			worker->waiting.id = 0;
		}
	}
	return id;
}

// Calling by Scheduler, may consume service_done
static inline service_id
worker_done_job(struct worker_thread *worker) {
	int done = worker->service_done;
	if (done) {
		// only one consumer (Scheduler) , so don't need use CAS to set
		worker->service_done = 0;
	}
	service_id r = { done };
	return r;
}

// Calling by Worker, may produce service_done. 0 : succ
static inline int
worker_complete_job(struct worker_thread *worker) {
	if (atomic_int_cas(&worker->service_done, 0, worker->running.id)) {
		worker->running.id = 0;
		return 0;
	}
	return 1;
}

#endif
