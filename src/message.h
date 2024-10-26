#ifndef ltask_message_h
#define ltask_message_h

#include <stddef.h>
#include "service.h"

typedef unsigned int session_t;

#define MESSAGE_SYSTEM 0
#define MESSAGE_REQUEST 1
#define MESSAGE_RESPONSE 2
#define MESSAGE_ERROR 3
#define MESSAGE_SIGNAL 4
#define MESSAGE_IDLE 5

#define MESSAGE_RECEIPT_NONE 0
#define MESSAGE_RECEIPT_DONE 1
#define MESSAGE_RECEIPT_ERROR 2
#define MESSAGE_RECEIPT_BLOCK 3
#define MESSAGE_RECEIPT_RESPONCE 4

// If to == 0, it's a schedule message. It should be post from root service (1).
// type is MESSAGE_SCHEDULE_* from is the parameter (for DEL service_id).
#define MESSAGE_SCHEDULE_NEW 0
#define MESSAGE_SCHEDULE_DEL 1

/*
	struct message 用于表示在服务之间传递的消息。

    典型用途：
 		消息传递：struct message 是 Ltask 系统中服务之间进行异步通信的基本单元，支持灵活的消息格式和类型。
		请求-响应机制：通过 session 和 type 字段，系统可以实现高效的请求-响应机制，确保请求和相应的正确匹配。
*/
struct message {
	// 这是一个 service_id 类型的字段，表示消息的发送者。通过这个 ID，接收方可以知道消息是从哪个服务发送的。
	service_id from;

	// 与 from 类似，这是一个 service_id 类型的字段，表示消息的接收者。它帮助调度系统将消息正确地路由到目标服务。
	service_id to;

	// 这是一个 session_t 类型的字段，用于标识特定的会话。会话 ID 通常用于请求和响应的匹配，确保接收到的响应能够正确关联到相应的请求。
	session_t session;

	// 表示消息的类型。根据应用的需求，这个字段可以用于区分不同类型的消息，比如请求、响应或通知等。
	int type;

	// 指向消息内容的指针。这个字段可以指向任何类型的数据，具体内容由发送方决定。
	void *msg;

	// 表示消息内容的大小，通常以字节为单位。这有助于接收方了解如何处理和释放消息数据。
	size_t sz;
};

struct message * message_new(struct message *msg);
void message_delete(struct message *msg);


#endif
