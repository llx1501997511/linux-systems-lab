#ifndef __THREADPOOL_H_
#define __THREADPOOL_H_

/* ==================================================================
 * 线程池对外接口
 *
 * 使用者只需要 create / add / destroy 三个函数；
 * all_threadnum 和 busy_threadnum 用于观测运行状态。
 *
 * 任务队列是一段固定大小的环形数组：front 指向队头元素，
 * rear 指向下一个空位。只用 front/rear 无法区分「空」和「满」，
 * 所以额外用 queue_size 记录当前个数。
 *
 * 两把锁分工：lock 保护队列和线程名册，thread_counter 单独保护
 * busy_thr_num，这样统计忙碌数时不用和入队出队抢同一把锁。
 * ================================================================== */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>

/* 巡检周期（秒） / 触发扩容的最小积压任务数 / 单次扩缩容上限 */
#define DEFAULT_TIME 10
#define MIN_WAIT_TASK_NUM 10
#define DEFAULT_THREAD_VARY 10

/* 队列里的一格：做什么 + 拿什么去做 */
typedef struct{
    void *(*function)(void *);
    void *arg;
}threadpool_task_t;

typedef struct threadpool_t{
    /* 锁与条件变量 */
    pthread_mutex_t lock;
    pthread_mutex_t thread_counter;

    pthread_cond_t queue_not_full;
    pthread_cond_t queue_not_empty;

    /* 线程：名册指针 + 巡检线程 tid */
    pthread_t *threads;
    pthread_t adjust_tid;

    /* 环形队列 */
    threadpool_task_t *task_queue;
    int queue_front;
    int queue_rear;
    int queue_size;
    int queue_max_size;

    /* 编制：最小 / 最大 / 在册 / 忙碌 / 待退出 */
    int min_thr_num;
    int max_thr_num;
    int live_thr_num;
    int busy_thr_num;
    int wait_exit_thr_num;

    /* 关闭标志 */
    int shutdown;

}threadpool_t;

threadpool_t *threadpool_create(int min_thr_num,int max_thr_num,int queue_maxsize);
int threadpool_add(threadpool_t *pool,void *(*function)(void *arg),void *arg);
int threadpool_destroy(threadpool_t *pool);
int threadpool_all_threadnum(threadpool_t *pool);
int threadpool_busy_threadnum(threadpool_t *pool);


#endif
