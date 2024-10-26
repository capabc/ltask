#ifndef ltask_cond_h
#define ltask_cond_h

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)

#include <windows.h>

// struct cond 是一个用于线程同步的条件变量结构体
// 这种设计使得 struct cond 可以在多线程环境下有效地进行条件等待与唤醒操作，在任务调度、同步控制等场景中很有用。
struct cond {
	// CONDITION_VARIABLE 类型的字段,用于条件变量。条件变量通常用于线程之间的同步，允许线程等待某个条件满足后再继续执行。
    CONDITION_VARIABLE c;

	// SRWLOCK 类型的字段，这是 Windows 上的共享-独占锁（Slim Reader/Writer Lock），用于保护 c 变量或其他共享数据，以确保线程安全。它能高效地控制多个线程对资源的读写访问。
    SRWLOCK lock;

	// 一个整型标志位，用于指示当前条件的状态。flag 可以用于标记条件是否已经满足，从而决定是继续等待还是解除阻塞
    int flag;
};

static inline void
cond_create(struct cond *c) {
	memset(c, 0, sizeof(*c));
}

static inline void
cond_release(struct cond *c) {
    (void)c;
}

static inline void
cond_trigger_begin(struct cond *c) {
    AcquireSRWLockExclusive(&c->lock);
	c->flag = 1;
}

static inline void
cond_trigger_end(struct cond *c, int trigger) {
    if (trigger) {
	    WakeConditionVariable(&c->c);
    } else {
        c->flag = 0;
    }
	ReleaseSRWLockExclusive(&c->lock);
}

static inline void
cond_wait_begin(struct cond *c) {
	AcquireSRWLockExclusive(&c->lock);
}

static inline void
cond_wait_end(struct cond *c) {
	c->flag = 0;
    ReleaseSRWLockExclusive(&c->lock);
}

static inline void
cond_wait(struct cond *c) {
	while (!c->flag)
        SleepConditionVariableSRW(&c->c, &c->lock, INFINITE, 0);
}

#else

#include <pthread.h>

struct cond {
    pthread_cond_t c;
    pthread_mutex_t lock;
    int flag;
};

static inline void
cond_create(struct cond *c) {
	pthread_mutex_init(&c->lock, NULL);
	pthread_cond_init(&c->c, NULL);
	c->flag = 0;    
}

static inline void
cond_release(struct cond *c) {
	pthread_mutex_destroy(&c->lock);
	pthread_cond_destroy(&c->c);
}

static inline void
cond_trigger_begin(struct cond *c) {
	pthread_mutex_lock(&c->lock);
	c->flag = 1;
}

static inline void
cond_trigger_end(struct cond *c, int trigger) {
    if (trigger) {
	    pthread_cond_signal(&c->c);
    } else {
        c->flag = 0;
    }
	pthread_mutex_unlock(&c->lock);
}

static inline void
cond_wait_begin(struct cond *c) {
	pthread_mutex_lock(&c->lock);
}

static inline void
cond_wait_end(struct cond *c) {
	c->flag = 0;
    pthread_mutex_unlock(&c->lock);
}

static inline void
cond_wait(struct cond *c) {
	while (!c->flag)
		pthread_cond_wait(&c->c, &c->lock);
}

#endif

#endif
