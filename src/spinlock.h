#ifndef ltask_spinlock_h
#define ltask_spinlock_h

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)

// windows下实现
#include <windows.h>

struct spinlock {
	SRWLOCK lock;
};

static inline int
spinlock_init(struct spinlock *sp) {
	// 初始化一个 SRWLock 对象
	InitializeSRWLock(&sp->lock);
	return 0;
}

static inline int
spinlock_destroy(struct spinlock *sp) {
	/*
		与 POSIX 的互斥锁 pthread_mutex_t 不同，Windows 的 SRWLock 是一种轻量级的锁，它并不需要显式的销毁。
		SRWLock 的生命周期通常与其所在的对象的生命周期相同，当 SRWLock 对象不再需要时，操作系统会自动回收它所占的资源。
		因此，spinlock_destroy 函数在此处不需要做任何事，直接返回 0 即可。
	*/
	return 0;
}

/*
	获取自旋锁，确保在多线程环境中对共享资源的独占访问
	SRWLock（Slim Reader/Writer Lock），SRWLock 是 Windows 平台提供的一种较轻量级的读写锁，适用于并发读、独占写的场景。
*/
static inline int
spinlock_acquire(struct spinlock *sp) {
	/*
		获取一个独占的自旋锁。
		这个函数是 Windows API 中的一个函数，用于实现线程安全的锁机制。
		调用此函数后，当前线程将会获得对该自旋锁的独占访问权，其他尝试获取该锁的线程会被阻塞，直到锁被释放。
	*/
	AcquireSRWLockExclusive(&sp->lock);

	// 函数返回 0，通常表示成功获取锁。虽然这个返回值在这里并没有其他用途，但可以为未来的扩展留出空间。
	return 0;
}

/*
	释放独占锁
*/
static inline int
spinlock_release(struct spinlock *sp) {
	// 释放独占锁（Exclusive Lock）的 Windows API，它会释放持有的 SRWLock 锁。
	ReleaseSRWLockExclusive(&sp->lock); 
	return 0;
}
/*
   尝试获取独占锁，如果锁已经被其他线程持有，则返回 0；否则返回 非零 值，表示成功获取锁
*/
static inline int
spinlock_try(struct spinlock *sp) {
	return !TryAcquireSRWLockExclusive(&sp->lock);
}

#else
// linux下的实现

#include <pthread.h>

/*
	传统的 spinlock 通常是通过不断检查锁的状态并在没有获得锁时持续“自旋”来避免线程进入阻塞状态。
 	然而，在linux下的实现使用的是 POSIX 的互斥锁 pthread_mutex_t，它的行为是线程在请求锁时会阻塞，直到获取到锁，而不是简单地自旋。
*/
struct spinlock {
	pthread_mutex_t mutex;
};

static inline int
spinlock_init(struct spinlock *lock) {
	// 初始化了一个基于 pthread_mutex_t 的互斥锁，并返回初始化状态
	return pthread_mutex_init(&lock->mutex, NULL);
}

static inline int
spinlock_destroy(struct spinlock *lock) {
	// 销毁一个已经初始化的互斥锁
	return pthread_mutex_destroy(&lock->mutex);
}

static inline int
spinlock_acquire(struct spinlock *lock) {
	// 将当前线程阻塞，直到获得锁。这不符合传统自旋锁的行为，因为自旋锁在获取不到锁时会不断尝试，而不进入阻塞状态
	return pthread_mutex_lock(&lock->mutex);
}

static inline int
spinlock_release(struct spinlock *lock) {
	// 正常地释放锁。
	return pthread_mutex_unlock(&lock->mutex);
}

static inline int
spinlock_try(struct spinlock *lock) {
	// 尝试获取锁。如果锁已经被其他线程持有，它会返回一个非零值而不是阻塞等待。这相对接近自旋锁的一部分行为，因为它不会让线程阻塞，而是尝试获取锁，如果失败就返回。
	return pthread_mutex_trylock(&lock->mutex);
}

#endif

#endif
