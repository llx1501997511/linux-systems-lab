#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include "threadpool.h"

static void *threadpool_thread(void *threadpool);
static void *adjust_thread(void *threadpool);
static int threadpool_free(threadpool_t *pool);

/* ==================================================================
 * 固定规模的 worker + 一个巡检线程的动态线程池
 *
 * 设计要点：
 *   1. 队列满时 add 阻塞、队列空时 worker 阻塞，两边都靠条件变量等，
 *      不在锁上自旋。
 *   2. 两把锁分工：pool->lock 保护队列和线程名册，thread_counter 单独
 *      保护 busy_thr_num，避免统计忙碌数时和入队出队争同一把锁。
 *   3. threads[] 是槽位池：worker 退出时把自己的槽位置 0，扩容时按 0
 *      找空位复用，否则反复扩缩会把名册耗光。
 *   4. 巡检线程每 DEFAULT_TIME 秒醒一次，按队列积压扩容、按空闲比例
 *      缩容，单次变动上限 DEFAULT_THREAD_VARY。
 * ================================================================== */
threadpool_t *threadpool_create(int min_thr_size,int max_thr_size,int queue_max_size){
    threadpool_t *pool=NULL;
    int i;
    int rc=0;
    do{
        /* 第 1 步：malloc 池子本体 */
        if((pool=(threadpool_t*)malloc(sizeof(threadpool_t)))==NULL){
            printf("malloc threadpool fail\n");
            break;
        }
         /* 第 2 步：填 9 个数字（min/max/live/busy/size/front/rear/maxsize/wait_exit/shutdown）*/
        pool->queue_front=0;
        pool->queue_rear=0;
        pool->queue_size=0;
        pool->queue_max_size=queue_max_size;

        pool->min_thr_num=min_thr_size;
        pool->max_thr_num=max_thr_size;
        pool->live_thr_num=min_thr_size;
        pool->busy_thr_num=0;
        pool->wait_exit_thr_num=0;

        pool->shutdown=false;

        /* 第 3 步：init 2 把锁 + 2 个条件变量。
         * 必须排在任何 malloc 之前：后面任何一步失败都会走到
         * threadpool_free，而它要销毁这四样东西；先 malloc 后 init 的话，
         * free 就会去销毁一把没 init 过的锁，属于未定义行为。 */
        rc|=pthread_mutex_init(&(pool->lock),NULL);
        rc|=pthread_mutex_init(&(pool->thread_counter),NULL);
        rc|=pthread_cond_init(&(pool->queue_not_empty),NULL);
        rc|=pthread_cond_init(&(pool->queue_not_full),NULL);
        if(rc!=0){
            printf("init the lock or cond fail\n");
            break;
        }

        /* 第 4 步：malloc threads[max_thr_num] */
        pool->threads=(pthread_t *)malloc(sizeof(pthread_t)*max_thr_size);
        if(pool->threads==NULL){
            printf("malloc threads fail\n");
            break;
        }
        memset(pool->threads,0,sizeof(pthread_t)*max_thr_size);
        /* 第 5 步：malloc task_queue[queue_max_size] */
        pool->task_queue=(threadpool_task_t *)malloc(sizeof(threadpool_task_t)*queue_max_size);
        if(pool->task_queue==NULL){
            printf("malloc task_queue fail\n");
            break;
        }
       
        /* 第 6 步：for 循环 pthread_create 招 min 个工人 */
        for(i=0;i<min_thr_size;i++){
            pthread_create(&(pool->threads[i]),NULL,threadpool_thread,(void *)pool);
            printf("start thread 0x%lx...\n",(unsigned long)pool->threads[i]);
        }
        /* 第 7 步：pthread_create 起工头 */
        pthread_create(&(pool->adjust_tid),NULL,adjust_thread,(void *)pool);
        return pool;
    }while(0);

    threadpool_free(pool);
    return NULL;
}
//往传送带上放任务
int threadpool_add(threadpool_t *pool,void *(*function)(void *arg),void *arg){
    //上锁，while(队满了)，wait（queuenotfull),入队，解锁，signal(queuenotempty)
    pthread_mutex_lock(&(pool->lock));
    while((pool->queue_size==pool->queue_max_size)&&(!pool->shutdown)){
        pthread_cond_wait(&(pool->queue_not_full),&(pool->lock));
    }
    if(pool->shutdown){
        pthread_cond_broadcast(&(pool->queue_not_empty));
        pthread_mutex_unlock(&(pool->lock));
        return 0;
    }
    // if(pool->task_queue[pool->queue_rear].arg!=NULL){
    //     pool->task_queue[pool->queue_rear]=NULL;
    // }
    pool->task_queue[pool->queue_rear].function=function;
    pool->task_queue[pool->queue_rear].arg=arg;
    pool->queue_rear=(pool->queue_rear+1)%pool->queue_max_size;
    pool->queue_size++;
    pthread_mutex_unlock(&(pool->lock));
    pthread_cond_signal(&(pool->queue_not_empty));
    return 0;
}
//工人执行任务，上锁，while(队伍空)，wait(queuenotempty),醒了，先看看自己是否要被裁，
//没有就拿任务，解锁
//上忙锁，busy++，做具体任务,干完了busy--,解小锁
static void *threadpool_thread(void *threadpool){
    threadpool_t *pool=(threadpool_t *)threadpool;
    threadpool_task_t task;
    while(1){
        pthread_mutex_lock(&(pool->lock));

        while(pool->queue_size==0&&(!pool->shutdown)){
            printf("thread 0x%lx is waiting\n",(unsigned long)pthread_self());
            pthread_cond_wait(&(pool->queue_not_empty),&(pool->lock));

            if(pool->wait_exit_thr_num>0&&pool->live_thr_num>pool->min_thr_num){
                pool->wait_exit_thr_num--;
                printf("thread 0x%lx is exiting\n",(unsigned long)pthread_self());
                pool->live_thr_num--;
                /* 辞职前把名册上自己的槽位置 0。不清的话这个位置永远带着
                 * 一个已死线程的 tid，而扩容靠 threads[j]==0 找空位，
                 * 就再也找不到它 —— 反复扩缩会把名册耗光，没到 max 也招不进人。
                 * 必须持着锁做：threads[] 正是工头扩容时要遍历的数组。 */
                for(int k=0;k<pool->max_thr_num;k++){
                    if(pool->threads[k]==pthread_self()){
                        pool->threads[k]=0;
                        break;
                    }
                }
                pthread_mutex_unlock(&(pool->lock));
                pthread_exit(NULL);
              
            }
        }
        if(pool->shutdown){
            pthread_mutex_unlock(&(pool->lock));
            printf("thread 0x%lx is exiting\n",(unsigned long)pthread_self());
            
            pthread_exit(NULL);
        }
        task.function=pool->task_queue[pool->queue_front].function;
        task.arg=pool->task_queue[pool->queue_front].arg;

        pool->queue_front=(pool->queue_front+1)%pool->queue_max_size;
        pool->queue_size--;

        pthread_cond_broadcast(&(pool->queue_not_full));
        pthread_mutex_unlock(&(pool->lock));

        printf("thread 0x%lx start working\n",(unsigned long)pthread_self());
        pthread_mutex_lock(&(pool->thread_counter));
        pool->busy_thr_num++;
        pthread_mutex_unlock(&(pool->thread_counter));

        (*(task.function))(task.arg);

        printf("thread 0x%lx end working\n",(unsigned long)pthread_self());
        pthread_mutex_lock(&(pool->thread_counter));
        pool->busy_thr_num--;
        pthread_mutex_unlock(&(pool->thread_counter));
    }    
}
/* 巡检线程：每 DEFAULT_TIME 秒醒一次，读三个数决定招人还是裁人。
 *   queue_size 里还压着多少活  -> 积压就扩容
 *   live        现在几个工人    -> 决定还能不能加
 *   busy        几个在干活      -> 闲人过半就缩容
 * 三个数分两把锁读，读完就放锁再判断，不持锁睡觉。 */
static void *adjust_thread(void *threadpool){
    threadpool_t *pool=(threadpool_t *)threadpool;
    while(!pool->shutdown){
        sleep(DEFAULT_TIME);
        
        pthread_mutex_lock(&(pool->lock));
        int queue_size=pool->queue_size;
        int live_num=pool->live_thr_num;
        pthread_mutex_unlock(&(pool->lock));

        pthread_mutex_lock(&(pool->thread_counter));
        int busy_num=pool->busy_thr_num;
        pthread_mutex_unlock(&(pool->thread_counter));
        //扩容，任务积没积压，人是不是没到上限
        if(queue_size>=MIN_WAIT_TASK_NUM&&live_num<pool->max_thr_num){
            pthread_mutex_lock(&(pool->lock));

            int add=0;
            for(int i=0;i<pool->max_thr_num&&add<DEFAULT_THREAD_VARY&&pool->live_thr_num<pool->max_thr_num;i++){
                if(pool->threads[i]==0){
                    pthread_create(&(pool->threads[i]),NULL,threadpool_thread,(void *)pool);
                    pool->live_thr_num++;
                    add++;
                }
            }
            pthread_mutex_unlock(&(pool->lock));
        }
        //缩容，注册的人是不是比2倍忙的人还多，并且没有到最小人数
        if((busy_num*2)<live_num&&live_num>pool->min_thr_num){
            pthread_mutex_lock(&(pool->lock));

            int num=pool->live_thr_num-pool->min_thr_num;
            num=(num<DEFAULT_THREAD_VARY)?num:DEFAULT_THREAD_VARY;
            pool->wait_exit_thr_num=num;

            pthread_mutex_unlock(&(pool->lock));

            pthread_cond_broadcast(&(pool->queue_not_empty));
            // for(int i=0;i<num;i++){
            //     pthread_cond_signal(&(pool->queue_not_empty));
            // }

           
            
        }

    }
    return NULL;
}

/* 五步：挂电闸 -> join 巡检线程 -> 广播叫醒所有 worker
 *       -> 逐个 join worker -> 释放内存
 * 顺序不能换：先叫醒 worker 再等巡检线程，巡检线程可能又招一批人。 */
int threadpool_destroy(threadpool_t *pool){
    int i;
    
    if(pool==NULL)
        return -1;
    
    pool->shutdown=true;
    
    pthread_join(pool->adjust_tid,NULL);

    pthread_cond_broadcast(&(pool->queue_not_empty));

    for(i=0;i<pool->max_thr_num;i++){
        if(pool->threads[i]!=0)
            pthread_join(pool->threads[i],NULL);
    }
    threadpool_free(pool);
    return 0;
}
//释放内存，create失败时也要用
static int threadpool_free(threadpool_t *pool){
    if(pool==NULL)
        return -1;

    if(pool->task_queue){
        free(pool->task_queue);
        pool->task_queue=NULL;
    }
        
    if(pool->threads){
        free(pool->threads);
        pool->threads=NULL;
    }
        
    pthread_mutex_destroy(&(pool->lock));
    pthread_mutex_destroy(&(pool->thread_counter));
    pthread_cond_destroy(&(pool->queue_not_empty));
    pthread_cond_destroy(&(pool->queue_not_full));

    free(pool);
    pool=NULL;
    return 0;
    
}

int threadpool_all_threadnum(threadpool_t *pool){
    int all_threadnum=-1;
    pthread_mutex_lock(&(pool->lock));
    all_threadnum=pool->live_thr_num;
    pthread_mutex_unlock(&(pool->lock));
    return all_threadnum;
}

int threadpool_busy_threadnum(threadpool_t *pool){
    int busy_threadnum=-1;
    pthread_mutex_lock(&(pool->thread_counter));
    busy_threadnum=pool->busy_thr_num;
    pthread_mutex_unlock(&(pool->thread_counter));
    return busy_threadnum;
}
