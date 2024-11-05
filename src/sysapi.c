#include "sysapi.h"

#ifndef _WIN32

#include <assert.h>
#include <errno.h>
#include <time.h>

void
sys_init() {
}

void
sys_sleep(unsigned int msec) {
	struct timespec timeout;
	int rc;
	timeout.tv_sec  = msec / 1000;
	timeout.tv_nsec = (msec % 1000) * 1000 * 1000;
	do
		rc = nanosleep(&timeout, &timeout);
	while (rc == -1 && errno == EINTR);
	assert(rc == 0);
}

#else

#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#    define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2
#endif

NTSTATUS NTAPI NtSetTimerResolution(ULONG RequestedResolution, BOOLEAN Set, PULONG ActualResolution);

static int support_hrtimer = 0;

/*
	sys_init ，用于初始化系统的高分辨率计时器支持
*/
void
sys_init() {
	/*
		创建一个可等待的高分辨率计时器。
		参数说明：
			NULL: 没有指定安全属性。
			NULL: 计时器的名称为默认值。
			CREATE_WAITABLE_TIMER_HIGH_RESOLUTION: 这个标志表示创建高分辨率计时器。
			TIMER_ALL_ACCESS: 允许所有权限访问计时器。
	*/
	HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

	// 如果CreateWaitableTimerExW返回 NULL，说明计时器创建失败，此时将全局变量 support_hrtimer 设置为 0，表示不支持高分辨率计时器。
	if (timer == NULL) {
		support_hrtimer = 0;
	} else {
		// 如果计时器创建成功，调用 CloseHandle(timer) 关闭计时器句柄，并将 support_hrtimer 设置为 1，表示支持高分辨率计时器。
		//	注释中提到该功能支持在 Windows 10 1803 及以上版本。
		CloseHandle(timer);
		// supported in Windows 10 1803+
		support_hrtimer = 1;
	}
}
/*

	高分辨率计时器是一种能够提供比传统计时器更高精度的计时机制。它们通常用于需要精确时间测量和控制的应用程序，
		例如多媒体处理、游戏开发、实时系统和高性能计算等。
		
	以下是高分辨率计时器的一些关键特性：

	1. 精度高
	高分辨率计时器能够以更短的时间间隔（通常在微秒级别或更小）进行计时，相比于标准计时器（通常以毫秒为单位），其精度更高。

	2. 准确性好
	高分辨率计时器通常具有更低的漂移和误差，可以提供更可靠的时间测量，适合对时间敏感的任务。

	3. 可等待
	一些高分辨率计时器允许线程等待特定的时间段，能够在指定时间到达时唤醒线程。这种特性对多线程编程非常有用。

	4. 适用场景
	高分辨率计时器广泛应用于：

	多媒体应用：音视频播放和录制需要精确的时间控制。
	游戏开发：游戏的帧率和物理引擎的更新需要高精度计时。
	实时系统：需要在严格的时间约束下执行任务的系统，如工业控制、航空航天等。

	5. 操作系统支持
	高分辨率计时器的实现和支持可能因操作系统而异。
	例如，在 Windows 系统中，可以通过 CreateWaitableTimerEx 等函数创建高分辨率计时器，而在 Linux 系统中可以使用 clock_gettime 等函数。

	总体而言，高分辨率计时器为需要精确时间控制的应用提供了强大的支持，能够提高系统的响应速度和处理能力。
*/



static void hrtimer_start() {
	if (!support_hrtimer) {
		ULONG actual_res = 0;
		NtSetTimerResolution(10000, TRUE, &actual_res);
	}
}

static void hrtimer_end() {
	if (!support_hrtimer) {
		ULONG actual_res = 0;
		NtSetTimerResolution(10000, FALSE, &actual_res);
	}
}

void
sys_sleep(unsigned int msec) {
	HANDLE timer = CreateWaitableTimerExW(NULL, NULL, support_hrtimer ? CREATE_WAITABLE_TIMER_HIGH_RESOLUTION : 0, TIMER_ALL_ACCESS);
	if (!timer) {
		return;
	}
	hrtimer_start();
	LARGE_INTEGER time;
	time.QuadPart = -((long long)msec * 10000);
	if (SetWaitableTimer(timer, &time, 0, NULL, NULL, 0)) {
		WaitForSingleObject(timer, INFINITE);
	}
	CloseHandle(timer);
	hrtimer_end();
}

#endif
