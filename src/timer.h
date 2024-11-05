#ifndef ltask_timer_h
#define ltask_timer_h

#include <stdint.h>

/*
 导读：

 两个结构体： 定时器 timer; 定时器节点 timer_node
*/

struct timer;

typedef void (*timer_execute_func)(void *ud,void *arg);

// 初始化一个定时器
struct timer * timer_init();

// 销毁一个定时器
void timer_destroy(struct timer *T);

// 
uint64_t timer_now(struct timer *TI);

//
uint32_t timer_starttime(struct timer *TI);

//
void timer_update(struct timer *TI, timer_execute_func func, void *ud);

// 添加一个新的定时器节点
void timer_add(struct timer *T,void *arg,size_t sz,int time);

#endif