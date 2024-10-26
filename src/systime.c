#if defined(__MINGW32__) || defined(__MINGW64__)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

#include <time.h>

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#include <sys/time.h>
#include <mach/task.h>
#include <mach/mach.h>
#endif

#include "systime.h"
/*
	返回的是自 1970 年 1 月 1 日以来的毫秒时间戳
*/
uint64_t
systime_wall() {
	// 声明一个 uint64_t 类型的变量 t，用于存储计算得到的时间戳
	uint64_t t;


#if defined(_WIN32) // Windows 系统处理
	// 使用 GetSystemTimeAsFileTime 获取系统时间，存储在 FILETIME 结构中
	FILETIME f;
	GetSystemTimeAsFileTime(&f);

	// 将高位和低位时间合并为一个 64 位的时间戳
	t = ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime;
	t = t / (uint64_t)100000 - (uint64_t)1164447360000; // 转换为自1970年1月1日以来的毫秒

#elif !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER) // 非 macOS 系统处理
	// 使用 clock_gettime 函数获取当前的系统时间，存储在 timespec 结构中。
	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti);

	// 将秒数转换为毫秒，并将纳秒转换为毫秒（/ 10000000），将二者相加。
	t = (uint64_t)ti.tv_sec * 100 + (ti.tv_nsec / 10000000);

#else // macOS 或其他系统处理：
	// 使用 gettimeofday 函数获取当前的时间，存储在 timeval 结构中。
	struct timeval tv;
	gettimeofday(&tv, NULL);

	// 将秒数转换为毫秒，并将微秒转换为毫秒（/ 10000），将二者相加。
	t = (uint64_t)tv.tv_sec * 100 + tv.tv_usec / 10000;
#endif

	// 返回计算得到的时间戳 t。
	return t;
}

/*
	获取自系统启动以来的高精度时间, 以100微秒为单位
*/
uint64_t
systime_mono() {
	uint64_t t;
#if defined(_WIN32) // Windows 系统处理
	// 获取系统启动以来的毫秒数，转换为100微秒单位 
	t = GetTickCount64() / 10;
#elif !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER) // 非 macOS 系统处理：
	// 使用 clock_gettime(CLOCK_MONOTONIC, &ti) 获取单调时间，存储在 timespec 结构中。
	struct timespec ti;
	clock_gettime(CLOCK_MONOTONIC, &ti);
	// 将秒数转换为 100 微秒（* 100），并将纳秒转换为 100 微秒（/ 10000000），两者相加得到总的 100 微秒数。
	t = (uint64_t)ti.tv_sec * 100;
	t += ti.tv_nsec / 10000000;
#else
	// 使用 gettimeofday 函数获取当前时间，存储在 timeval 结构中。
	struct timeval tv;
	gettimeofday(&tv, NULL);
	// 将秒转换为 100 微秒，并将微秒转换为 100 微秒（/ 10000），两者相加。
	t = (uint64_t)tv.tv_sec * 100;
	t += tv.tv_usec / 10000;
#endif
	return t;
}

static inline uint64_t
systime_counter_(int thread_timer) {
#if defined(_WIN32)
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	uint64_t i64 = li.QuadPart;
#elif !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER)
	struct timespec now;
	int id = thread_timer ? CLOCK_THREAD_CPUTIME_ID : CLOCK_MONOTONIC;
	clock_gettime(id, &now);
	uint64_t i64 = now.tv_sec*(uint64_t)(1000000000) + now.tv_nsec;
#else
	struct timeval now;
	gettimeofday(&now, 0);
	uint64_t i64 = now.tv_sec*(uint64_t)(1000000) + now.tv_usec;
#endif
	return i64;
}

uint64_t
systime_counter() {
	return systime_counter_(0);
}

uint64_t
systime_thread() {
	return systime_counter_(1);
}

uint64_t
systime_frequency() {
#if defined(_WIN32)
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	return li.QuadPart;
#elif !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER)
	return (uint64_t)(1000000000);
#else
	return (uint64_t)(1000000);
#endif
}
