#ifndef ltask_spinlock_h
#define ltask_spinlock_h

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)

#include <windows.h>

struct spinlock {
	SRWLOCK lock;
};

static inline int
spinlock_init(struct spinlock *sp) {
	InitializeSRWLock(&sp->lock);
	return 0;
}

static inline int
spinlock_destroy(struct spinlock *sp) {
	return 0;
}

/*
	获取自旋锁，确保在多线程环境中对共享资源的独占访问
*/
static inline int
spinlock_acquire(struct spinlock *sp) {
	// 调用获取一个独占的自旋锁。这个函数是 Windows API 中的一个函数，用于实现线程安全的锁机制。
		// 调用此函数后，当前线程将会获得对该自旋锁的独占访问权，其他尝试获取该锁的线程会被阻塞，直到锁被释放。
	AcquireSRWLockExclusive(&sp->lock);

	// 函数返回 0，通常表示成功获取锁。虽然这个返回值在这里并没有其他用途，但可以为未来的扩展留出空间。
	return 0;
}

static inline int
spinlock_release(struct spinlock *sp) {
	ReleaseSRWLockExclusive(&sp->lock);
	return 0;
}

static inline int
spinlock_try(struct spinlock *sp) {
	return !TryAcquireSRWLockExclusive(&sp->lock);
}

#else

#include <pthread.h>

struct spinlock {
	pthread_mutex_t mutex;
};

static inline int
spinlock_init(struct spinlock *lock) {
	return pthread_mutex_init(&lock->mutex, NULL);
}

static inline int
spinlock_destroy(struct spinlock *lock) {
	return pthread_mutex_destroy(&lock->mutex);
}

static inline int
spinlock_acquire(struct spinlock *lock) {
	return pthread_mutex_lock(&lock->mutex);
}

static inline int
spinlock_release(struct spinlock *lock) {
	return pthread_mutex_unlock(&lock->mutex);
}

static inline int
spinlock_try(struct spinlock *lock) {
	return pthread_mutex_trylock(&lock->mutex);
}

#endif

#endif
