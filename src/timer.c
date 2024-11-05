#include "spinlock.h"
#include "systime.h"
#include "timer.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)	// 其值为 2^8 = 256。这个常量表示在时间管理中，近期的时间范围 (在这个实现里，是25.6ms)
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)  // 其值为 2^6 = 64。这个常量表示一个时间层级的大小，通常用于管理更长时间范围的定时器。
#define TIME_NEAR_MASK (TIME_NEAR-1)		// 其值为 255。这是一个掩码，用于提取时间值的低 8 位，以判断某个时间是否在近期的时间段内。
#define TIME_LEVEL_MASK (TIME_LEVEL-1)		// 其值为 63。这是一个掩码，用于提取时间值的低 6 位，以判断某个时间值属于哪个时间层级。


/*
	ps： 阅读脉络

	1. 相关结构体
		timer / timer_node

	2. 主要函数：
		
		// 定时器创建、初始化
		struct timer * timer_init(); 
			timer_new(..)
		
		// 添加一个新的定时器节点
		void timer_add(struct timer *T,void *arg,size_t sz,int time);
			add_node(.)

		// 更新定时器的状态并处理定时器到期的事件
		void timer_update(struct timer *TI, timer_execute_func func, void *ud);
			timer_update_tick(..)
				timer_execute(..)
				timer_shift(..)
				timer_execute(..)

		
		// 定时器销毁
		void timer_destroy(struct timer *T);
*/


// struct timer_node 用于表示定时器链表中的节点
struct timer_node {
	// 指向下一个定时器节点的指针。通过链表结构，多个定时器可以以串联的方式存储和管理，使得遍历和查找定时器变得更加高效。
	struct timer_node *next;

	// 一个无符号 32 位整数，表示定时器的到期时间。这个时间通常以某个基准时间（如当前时间）为参考，指示该定时器何时应该被触发
	uint32_t expire;
};


struct link_list {
	// 链表的头节点，类型为 struct timer_node。它不仅包含对下一个节点的指针，还可能存储链表中的某些元数据（如到期时间）。
		// 头节点的存在使得操作链表（如插入、删除等）更加高效，因为可以避免特殊处理空链表的情况。
		// 在这里，头结点 ≠ 第一个有效的节点。
	struct timer_node head;

	// 指向链表尾部节点的指针。
		// 通过直接维护对尾节点的引用，可以在需要时快速地向链表末尾添加新节点，提高性能。
	struct timer_node *tail;
};



/*
	struct timer 用于管理定时器的功能。
		旨在提供高效、精确的定时功能，以支持复杂的调度需求和高并发环境下的任务管理。

	典型用途：
		定时任务调度：struct timer 支持定时任务的调度和管理，可以用于实现周期性任务或延时执行的功能。
		事件驱动编程：定时器结构在事件驱动编程中扮演重要角色，能够在特定时间触发事件或调用回调函数，提升系统的响应能力。
	此结构体设计旨在提供高效、精确的定时功能，以支持复杂的调度需求和高并发环境下的任务管理

	ps：这里面的时间相关字段，时间单位统一为0.1ms；
*/
struct timer {
	// 这是一个数组，存储即将到期的定时器的链表。TIME_NEAR :256，25.6ms内到期的节点将放到这个数组的某个slot内的链表内。
	struct link_list n[TIME_NEAR];
	
	// 一个二维数组，存储按时间级别分组的定时器链表。这个结构允许快速查找和管理不同时间段内的定时器。TIME_LEVEL:64
	struct link_list t[4][TIME_LEVEL];

	// 自旋锁，用于确保对 timer 结构体的线程安全访问。在多线程环境中，自旋锁可以防止数据竞争。
	struct spinlock lock;

	// 这个time 在timer_shift()中不断+1， 它和current字段类似，但是总是相对的滞后于current字段。
	uint32_t time;

	// 定时器启动时的系统时间 timer_init()中初始化
	uint32_t starttime;

	// current 用于跟踪定时器的运行时间。 timer_init()中初始化
		// 它记录了自上一次 timer_update()调用后， timer已运行的时间总量。
	uint64_t current;

	// 自系统启动以来的时间值。 timer_init()中初始化
	uint64_t current_point;

	// 指向定时器到期时调用的回调函数的指针。用户可以定义自己的处理函数，以在定时器到期时执行特定操作。
	timer_execute_func func;

	// 指向用户自定义数据的指针，允许用户在回调函数中使用额外的信息，例如上下文或状态。
	void *ud;
};

/*
	清空链表，并返回原链表中第一个有效节点的指针
*/ 
static inline struct timer_node *
link_clear(struct link_list *list) {
	// 取得第一个有效节点
	struct timer_node * ret = list->head.next;
	// 清空链表，将头节点的下一个指针置为 NULL。
	list->head.next = 0;
	// 结尾点指向头节点
	list->tail = &(list->head);

	return ret;
}



/*
	将新节点链接到链表的尾部。
*/
static inline void
link(struct link_list *list,struct timer_node *node) {
	list->tail->next = node; // 将新节点链接到链表的尾部
	list->tail = node;		 // 更新尾指针为新加入的节点。
	node->next = 0;			 // 将尾指针重置为指向头节点。
}

/*
	将一个定时器节点添加到合适的链表中，以便进行定时管理。
*/
static void
add_node(struct timer *T,struct timer_node *node) {
	// 获取timer_node的到期时间。
	uint32_t time = node->expire;

	// 获取当前时间。
	uint32_t current_time = T->time;
	
	// 检查节点的到期时间与当前时间是否在同一个 TIME_NEAR 时间段内。（即是否25.6ms内就要到期了）
	if ((time|TIME_NEAR_MASK) == (current_time|TIME_NEAR_MASK)) {
		// 如果是，将节点链接到 TIME_NEAR 的链表中， TIME_NEAR 也分为256个等级，应对不同的到期的时间，ps:这样，精度被控制在0.1ms？？
		link(&T->n[time & TIME_NEAR_MASK],node);
	} else {
		int i;
		uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT; // // 初始化时间掩码。2 << 8 << 6
		/*
			mask 的初始值是 TIME_NEAR << TIME_LEVEL_SHIFT。
			由于 TIME_NEAR 的值是 256（，而 TIME_LEVEL_SHIFT 的值是 6，所以：初始 mask = 256 << 6 = 16384。
			
			每次迭代：

			每次循环，mask 都会左移 TIME_LEVEL_SHIFT（即 6 位），所以 mask 的值将变为：
			第一次迭代：mask = 16384。 
			第二次迭代：mask = 16384 << 6 = 1048576。
			第三次迭代：mask = 1048576 << 6 = 67108864。

			经过三次迭代后，mask 的最大值为 67108864，即 2^26。

		*/

		// 遍历检查节点的到期时间属于哪个时间级别，
			// 在这里&T->t 给了4个级别，每个级别下是一个长度为64的数组，数组内的每个slot是一个链表。
		for (i=0;i<3;i++) {
			 // 如果当前时间的高位部分与到期时间相同，则找到合适的时间级别。
			if ((time | (mask-1)) == (current_time | (mask-1))) {
				break;
			}
			mask <<= TIME_LEVEL_SHIFT; // 更新掩码，检查下一个级别。
		}

		// 将节点链接到对应时间级别的数组内，
			// 在二维数组里又会再次分配到某个slot内的链表下。
		link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)],node);	
	}
}

/*
	通过分配内存、复制用户数据并设置到期时间，安全地将 新的定时器节点 添加到定时器中
	T ：  定时器实例
	arg ：用户提供的数据指针
	sz ： 用户提供的数据大小；
	time ：待触发的一个时间段， 单位应该也是0.11
*/
void
timer_add(struct timer *T,void *arg, size_t sz, int time) {
	// 分配内存以容纳定时器节点及其附加的数据
	struct timer_node *node = (struct timer_node *)malloc(sizeof(*node) + sz);

	// 将用户提供的 arg 数据复制到 node 的后面（node + 1 处）。
		// 这样，节点结构体后面紧跟着的是用户数据
		// 后续就可以使用node + 1 得到用户数据的指针， 即在dispatch_list()中看到过的current+1 使用方式。
	memcpy(node + 1,arg,sz);

	//  获取自旋锁，确保在修改定时器状态时不会有其他线程干扰。
	spinlock_acquire(&T->lock);

	//  设置定时器节点的到期时间。这个到期时间是基于当前的 T->time 加上传入的 time 参数，确保定时器能够在预期的时间后触发.
		// T->time 在上面已经说明过了，它是一个时间计数器，以100微秒为单位，即0.1ms。
	node->expire = time + T->time;

	// 将新的定时器节点添加到定时器管理的链表中
	add_node(T, node);

	// 释放自旋锁，允许其他线程访问定时器。
	spinlock_release(&T->lock);
}

/*
	move_list 函数的主要作用是将从一个链表中清除的定时器节点重新添加到另一个链表中。
	通过这种方式，函数确保定时器可以根据其到期时间灵活地在不同的链表间移动，从而实现高效的定时器管理。
*/
static void
move_list(struct timer *T, int level, int idx) {
	// 用 link_clear 函数清空指定层级和索引的链表，并返回该链表的首个节点（即将要移动的定时器节点）
	struct timer_node *current = link_clear(&T->t[level][idx]);

	while (current) {
		// 在处理当前节点之前，先保存下一个节点，以便在当前节点处理完后继续遍历。
		struct timer_node *temp=current->next;
		//  将当前节点添加到新的链表中。这可能是在其他层级或链表中，以实现定时器的调度。
		add_node(T,current);
		// 更新 current 指针，指向下一个节点，继续循环处理
		current=temp;
	}
}

/*
	timer_shift 函数通过更新当前时间并检查到期的定时器，确保及时将它们移动到相应的链表中，以便后续处理。
	这种设计使得定时器的管理更加高效，能够灵活应对不同的到期时间。
*/
static void
timer_shift(struct timer *T) {
	int mask = TIME_NEAR;

	//  T->time + 1, T->time的单位也是0.1ms，它在这里不断+1，直至追上T->current
	uint32_t ct = ++T->time;

	
	if (ct == 0) {
		// 特殊情况处理：当 ct 回绕至 0 时，特殊处理，调用 move_list 函数处理所有到期的定时器。
		move_list(T, 3, 0);
	} else {
		// 将当前时间右移，计算高位时间。
		uint32_t time = ct >> TIME_NEAR_SHIFT;  // >> 8
		int i=0;

		// 检查当前时间 ct 是否为 mask 的倍数。如果是，继续循环；否则，退出循环。
		/*
			1. 确定时间的特定状态
			通过位运算 (ct & (mask - 1))，可以有效地检查 ct 是否是 mask 的倍数。
			如果 mask 是一个 2 的幂，mask - 1 将会是一个所有低位为 1 的数。例如，如果 mask = 8（即 1000），那么 mask - 1 = 7（即 0111）。
			这样，只有在 ct 为 8 的倍数时，(ct & 7) 的结果才会是 0。

			2. 层级管理
			mask 的初始值为 TIME_NEAR(256)，在循环中逐渐左移（mask <<= TIME_LEVEL_SHIFT） (<<6)，用于检查更高层级的时间状态。
			这种方法允许函数逐层检查当前时间是否到达了特定的倍数，并相应地处理不同层级的定时器。

			3.优化处理
			当 ct 是 mask 的倍数时，意味着这是一个可能需要处理的时间点。此时，程序可以将到期的定时器移动到相应的链表中。
			这种机制确保了只在特定时间点才进行定时器的调度，减少了不必要的操作，优化了性能。

			4.避免溢出
			此判断还可以防止在时间计数器回绕时（即达到最大值后回到 0）发生不必要的错误或异常，确保 move_list 函数只在合适的时刻被调用
		*/
		while ((ct & (mask-1))==0) {
			// 计算对应的索引，表示当前时间的层级。
			int idx=time & TIME_LEVEL_MASK;  // idx = time & 63,

			// 如果 idx 不为 0，调用 move_list 函数移动到期的定时器，并退出循环。
			if (idx!=0) {
				move_list(T, i, idx);
				break;				
			}

			// 更新掩码和高位时间，为下一次检查做准备。
			mask <<= TIME_LEVEL_SHIFT; // mask <<= 6 即mask = mask * 64
			time >>= TIME_LEVEL_SHIFT; // time >>= 6 即time = time / 64
			++i;
		}
	}
}

/*
	负责遍历并处理一组timer_node。
	它通过调用用户指定的回调函数来执行相关操作，同时确保在处理后释放节点的内存。
	
	timer_execute_func函数指针：typedef void (*timer_execute_func)(void *ud,void *arg);
*/
static inline void
dispatch_list(struct timer_node *current, timer_execute_func func, void *ud) {
	do {
		// 调用用户提供的回调函数 func，传入的参数是 ud 和 (void *)(current + 1)。
			// 这里 current + 1 的作用是将指针向后移动一个位置，以访问定时器节点中的有效数据（假设有效数据存储在节点后面）。
			/*
				1. 内存布局
					struct timer_node 只有两个字段：next 和 expire。
					如果在 timer_node 结构体的后面存储与定时器相关的实际数据，那么 current + 1 将指向该数据的位置。

					在 C 语言中，指针加法是基于类型的，所以 current + 1 会使指针移动到 timer_node 结构体后面的位置，即 expire 字段后。

					它实际上指向的是用户传来的数据，可以在timer_add(struct timer *T, void *arg, size_t sz, int time) 中得到验证。
			*/
		func(ud, (void *)(current + 1));

		// 先保存当前节点的指针，以便在处理完后释放。
		struct timer_node * temp = current;

		// 更新 current 指向下一个定时器节点，以继续遍历
		current = current->next;

		// 在处理完当前节点后，释放其内存，防止内存泄漏
		free(temp);	
	} while (current);
}

/*
	执行到期的定时器事件
*/
static inline void
timer_execute(struct timer *T, timer_execute_func func, void *ud) {
	// 与255做&运算，即可得到一个索引idx，这个slot中链表的节点 都是到期的timer_node
	int idx = T->time & TIME_NEAR_MASK;
	
	// 遍历slot下的链表，只要这个链表不为空，就要及时处理，因为它们都到期了。
	while (T->n[idx].head.next) {

		// 将表头取出来，然后将链表清空； 这个表头current后面就挂着原链表。
		struct timer_node *current = link_clear(&T->n[idx]);

		// 释放自旋锁，以允许其他线程或操作访问定时器。这个步骤是为了避免在调用处理函数时持有锁，从而提高效率。
		spinlock_release(&T->lock);

		// dispatch_list don't need lock T
		// 调用 dispatch_list 函数处理获取的到期定时器节点。func 和 ud 是用户提供的参数。
		dispatch_list(current, func, ud);

		// 在处理完当前到期的定时器后，重新获取自旋锁，为接下来的操作做好准备。
		spinlock_acquire(&T->lock);

		// 继续检查链表非空， ps：释放自旋锁后，可能有新的快到期的timer_node插入进来，需要及时处理。
	}
}

/*
	处理定时器的到期事件，并确保在多线程环境下的安全性
*/
static void 
timer_update_tick(struct timer *T, timer_execute_func func, void *ud) {
	//  用于确保在多线程环境中，其他线程在处理定时器时不会修改其状态，从而避免数据竞争和不一致性。
	spinlock_acquire(&T->lock); 

	// 首先尝试执行任何超时为 0 的定时器事件。这种情况较少见，但在某些场景下可能会发生，例如定时器刚好到期时。
	// try to dispatch timeout 0 (rare condition)
	timer_execute(T, func, ud);

	// 这个步骤更新定时器的状态，将所有到期的定时器从内部数据结构中移除，并准备下一轮的定时器检查。
	// shift time first, and then dispatch timer message
	timer_shift(T); 

	// 再次检查并执行到期的定时器事件。这次调用是确保在时间移动后能处理所有到期的定时器。
	timer_execute(T, func, ud);

	// 在所有处理完成后，释放自旋锁，允许其他线程访问定时器。
	spinlock_release(&T->lock);
}

/*
	创建一个timer实例
*/
static struct timer *
timer_new() {
	// 分配内存
	struct timer *r=(struct timer *)malloc(sizeof(struct timer)); 

	// 填0
	memset(r,0,sizeof(*r)); 

	int i,j;

	// TIME_NEAR链表初始化
	for (i=0;i<TIME_NEAR;i++) {
		link_clear(&r->n[i]);
	}

	// 4个时间层级的链表初始化
	for (i=0;i<4;i++) {
		for (j=0;j<TIME_LEVEL;j++) {
			link_clear(&r->t[i][j]);
		}
	}

	// 初始化自旋锁
	spinlock_init(&r->lock); 

	// current填0， 稍后马上会被更新
	r->current = 0;	

	// 返回新的定时器实例
	return r;	
}

static void
timer_release_func(void *ud, void *arg) {
	// do nothing
	(void)ud;
	(void)arg;
}

/*
	函数负责清理和释放定时器资源，确保系统能够正确地释放所有相关的内存和资源
*/
void
timer_destroy(struct timer *T) {
	// 检查定时器指针是否为 NULL
	if (T == NULL)
		return;

	// 获取定时器的锁，确保线程安全
	spinlock_acquire(&T->lock);

	// 清理 TIME_NEAR 数组中的定时器节点
	int i,j;
	for (i=0;i<TIME_NEAR;i++) {
		// 清空链表并获取当前节点
		struct timer_node *current = link_clear(&T->n[i]);
		if (current) {
			// 调用释放函数
			dispatch_list(current, timer_release_func, NULL);
		}
	}

	// 清理 TIME_LEVEL 数组中的定时器节点
	for (i=0;i<4;i++) {
		for (j=0;j<TIME_LEVEL;j++) {
			// 清空链表并获取当前节点
			struct timer_node *current = link_clear(&T->t[i][j]);
			if (current) {
				// 调用释放函数
				dispatch_list(current, timer_release_func, NULL);
			}
		}
	}

	// 释放定时器的锁
	spinlock_release(&T->lock);

	// 销毁定时器的锁
	spinlock_destroy(&T->lock);

	// 释放定时器结构体的内存
	free(T);
}

/*
	timer_update 函数用于更新定时器的状态并处理定时器到期的事件
	func: 回调函数
	ud ： 用户数据
*/
void
timer_update(struct timer *TI, timer_execute_func func, void *ud) {
	// 获取自系统启动以来的经过的时间值， 以100微秒（即0.1ms）为单位
	uint64_t cp = systime_mono();

	if(cp < TI->current_point) {
		// 说明系统时间回退了，这通常是由于系统时间被调整或重启。此时打印错误信息，并更新 current_point 为当前时间。
		printf("time diff error: change from %" PRId64 " to %" PRId64, cp, TI->current_point);
		TI->current_point = cp;

	} else if (cp != TI->current_point) {

		// 如果 cp 和 current_point 不相等，计算二者的差值 diff，表示自上次执行timer_update()以后的时间间隔，单位是0.1ms。
		uint32_t diff = (uint32_t)(cp - TI->current_point);
		
		// 更新 current_point，自系统启动以来经过的时间值。 
		TI->current_point = cp;

		// 更新current， 加上diff， ps:也就是说，current 表示定时器已经运行的时间？
		TI->current += diff;

		// 通过一个循环，调用 timer_update_tick 函数处理每个时间间隔，执行定时器的到期处理逻辑。
		int i;
		for (i = 0;i < diff; i++) {
			timer_update_tick(TI, func, ud);
		}
		// 经过这个循环后， T->time = T->time + diff, 所以说，T->time 总是相对的滞后于T->current。
	}
}

uint32_t
timer_starttime(struct timer *TI) {
	return TI->starttime;
}

uint64_t 
timer_now(struct timer *TI) {
	return TI->current;
}

/*
	timer_init 函数用于初始化一个新的定时器实例
*/
struct timer *
timer_init() {
	// 创建新的定时器实例
	struct timer *TI = timer_new();	
	
	// 系统当前时间（毫秒）
	uint64_t walltime = systime_wall(); 
	
	// 定时器开始时间 为系统当前时间，以0.1ms为单位
	TI->starttime = walltime/100;	

	/*
		（walltime % 100）计算 walltime 在最后一个 100 毫秒周期内的位置；
		current实际上是以100微秒为单位的， 在这里它被初始化为 在最后一个 100 毫秒周期内的偏移量。

		举例：
		当前时间：walltime = 250 毫秒
		下一个 100 毫秒周期的开始：0 毫秒到 100 毫秒
		前一个 100 毫秒周期的结束：200 毫秒到 300 毫秒

		计算 walltime % 100
		对于 walltime = 250 毫秒，计算：
		current = walltime % 100 = 250 % 100 = 50 毫秒
		解释
		结果：current 为 50 毫秒，意味着在当前的 100 毫秒周期中（即 200 毫秒到 300 毫秒之间），当前时间点距离该周期的开始（200 毫秒）已经过去了 50 毫秒。

		那么 current的最大值为99 ， 在这里的计算它的用处是啥？
	*/
	TI->current = walltime % 100;	

	// 获取自系统启动以来的高精度时间，同样，以0.1ms为单位
		// 单调时间通常用于计时器，因为它不会受系统时间调整的影响。
	TI->current_point = systime_mono();

	return TI;
}
