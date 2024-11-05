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

	// 获取当前系统时间（从1601年1月1日起的100纳秒间隔数）。
	GetSystemTimeAsFileTime(&f);

	// 将高位和低位时间合并为一个 64 位的时间戳
	t = ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime;

	// 转换为自 1970年1月1日以来的毫秒数
		//  t = t / (uint64_t)10000000; 将 FILETIME 单位从 100 纳秒转换为秒。 （1秒 = 1000 0000 纳秒)
		//	(1164447360000 是从 1601年到 1970年的秒数）。
	t = t / (uint64_t)100000 - (uint64_t)1164447360000;

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
	获取自系统启动以来的高精度时间, 以100微秒 （即0.1ms）为单位
*/
uint64_t
systime_mono() {
	uint64_t t;
#if defined(_WIN32) // Windows 系统处理
	// 获取系统启动以来的毫秒数，然后除以10，转换为100微秒单位，即0.1ms
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
	// 将秒转换为 100 微秒，并将微秒转换为 100 微秒（ / 10000），两者相加。
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

/*
  用于获取系统的时钟频率;
  返回一个 uint64_t 类型的值，表示系统时钟的频率。

  	根据不同操作系统返回适合的时钟频率，以便其他定时或时间相关的功能可以根据此频率进行适当的计算。
  	在 Windows 中，它使用高精度计时器，而在其他系统中则返回一个固定值，分别对应于纳秒和微秒的时钟频率。
*/
uint64_t
systime_frequency() {
#if defined(_WIN32) // Windows系统
	LARGE_INTEGER li;
	// 使用 QueryPerformanceFrequency 函数获取高精度计时器的频率。
	QueryPerformanceFrequency(&li);
	// li.QuadPart 存储了频率值，单位是赫兹 (Hz)，即每秒的计数次数。
	return li.QuadPart;
#elif !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER) // 非 macOS 系统
	// 如果系统不是 macOS，或者是 macOS 10.12 及以上版本，则返回 1000000000，表示每秒 10 亿次，即 1 纳秒的频率。
	return (uint64_t)(1000000000);
#else // macOS 系统
	// 对于其他版本的 macOS，返回 1000000，表示每秒 100 万次，即 1 微秒的频率。
	return (uint64_t)(1000000);
#endif
}
