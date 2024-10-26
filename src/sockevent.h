#ifndef ltask_sockevent_h
#define ltask_sockevent_h

#include <string.h>

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

typedef SOCKET socket_t;
static const socket_t socket_invalid = INVALID_SOCKET;

static inline int
none_blocking_(socket_t fd) {
	unsigned long on = 1;
	return ioctlsocket(fd, FIONBIO, &on);
}

static inline void
sockevent_initsocket() {
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2,2), &wsaData);
}

#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

typedef int socket_t;
static const socket_t socket_invalid = -1;

#define closesocket close

static inline int
none_blocking_(socket_t fd) {
	return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

static inline void sockevent_initsocket() {}

#endif

#include "atomic.h"


// struct sockevent 用于实现与套接字相关的事件通知机制。该结构体包含以下字段：
// 通过 sockevent 结构体，可以实现基于套接字的事件通知机制，确保多线程或多进程系统中的事件可以安全、及时地传递和处理。、
struct sockevent {
	// 这是一个 socket_t 类型的数组，包含两个元素，通常用于创建一个双向通信的管道（或双端口套接字对）。
	//	在事件通知系统中，一个端口用于写入数据（触发事件），另一个端口用于读取数据（监听事件）。
	//	这种设计可以在跨线程或跨进程的场景下，实现事件的通知和传递。
	socket_t pipe[2];

	// atomic_int 类型的字段，用于表示当前事件状态。这一字段采用原子操作来进行状态更新，保证了多线程访问的安全性。
	//	通常，当事件触发时会更新 e 的状态，表明有新事件待处理。
	atomic_int e;
};

static inline void
sockevent_init(struct sockevent *e) {
	e->pipe[0] = socket_invalid;
	e->pipe[1] = socket_invalid;

	atomic_int_init(&e->e, 0);
}

static inline void
sockevent_close(struct sockevent *e) {
	if (e->pipe[0] != socket_invalid) {
		closesocket(e->pipe[0]);
		e->pipe[0] = socket_invalid;
	}
	if (e->pipe[1] != socket_invalid) {
		closesocket(e->pipe[1]);
		e->pipe[1] = socket_invalid;
	}
}

static inline int
sockevent_open(struct sockevent *e) {
	if (e->pipe[0] != socket_invalid)
		return 0;
	socket_t fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == socket_invalid)
		goto _error;
	e->pipe[0] = socket_invalid;
	e->pipe[1] = socket_invalid;
	struct sockaddr_in6 loopback;
	memset(&loopback, 0, sizeof(loopback));
	loopback.sin6_family = AF_INET6;
	loopback.sin6_addr = in6addr_loopback;
	loopback.sin6_port = 0;
	if (bind(fd, (const struct sockaddr *)&loopback, sizeof(loopback)) < 0)
		goto _error;
	socklen_t addrlen = sizeof(loopback);
	if (getsockname(fd, (struct sockaddr *)&loopback, &addrlen) < 0)
		goto _error;
	if (listen(fd, 32) < 0)
		goto _error;
	e->pipe[1] = socket(AF_INET6, SOCK_STREAM, 0);
	if (e->pipe[1] < 0)
		goto _error;
	if (none_blocking_(e->pipe[1]) < 0)
		goto _error;

	connect(e->pipe[1], (const struct sockaddr *)&loopback, sizeof(loopback));

	e->pipe[0] = accept(fd, (struct sockaddr *)&loopback, &addrlen);
	if (e->pipe[0] == socket_invalid)
		goto _error;

#ifdef SO_NOSIGPIPE
	const int enable = 1;
	if (0 != setsockopt(e->pipe[0], SOL_SOCKET, SO_NOSIGPIPE, (char*)&enable, sizeof(enable))) {
		goto _error;
	}
	if (0 != setsockopt(e->pipe[1], SOL_SOCKET, SO_NOSIGPIPE, (char*)&enable, sizeof(enable))) {
		goto _error;
	}
#endif
	int flags = 0;
#ifdef MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	char tmp[1] = { 0 };
	send(e->pipe[1], tmp, sizeof(tmp), flags);

	atomic_int_init(&e->e, 0);

	closesocket(fd);

	return 0;
_error:
	if (fd == socket_invalid)
		closesocket(fd);
	sockevent_close(e);
	return -1;
}

static inline void
sockevent_trigger(struct sockevent *e) {
	if (e->pipe[0] == socket_invalid)
		return;
	if (atomic_int_load(&e->e))
		return;

	atomic_int_store(&e->e, 1);
	int flags = 0;
#ifdef MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	char tmp[1] = { 0 };
	send(e->pipe[1], tmp, sizeof(tmp), flags);
}

static inline int
sockevent_wait(struct sockevent *e) {
	char tmp[128];
	int r = recv(e->pipe[0], tmp, sizeof(tmp), 0);
	atomic_int_store(&e->e, 0);
	return r;
}

static inline socket_t
sockevent_fd(struct sockevent *e) {
	return e->pipe[0];
}

#endif
