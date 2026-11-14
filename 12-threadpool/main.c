// 线程池的测试程序：建池子、推任务、每秒打印一次线程数、最后销毁。
//
// 用法：./test [任务数] [单任务秒数]，默认 60 个任务、每个 2 秒。
// 任务太少或太快跑完的话，等巡检线程 10 秒后醒来时队列已经空了，看不到扩容。
// 想看扩容，任务数 × 单任务秒数 ÷ 3 得明显大于 10 秒。
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "threadpool.h"

#define TASK_MAX 128            /* 栈上数组的上限，防止参数给大了越界 */

static int g_task_sec = 2;      /* 单任务耗时（秒），可被命令行第二个参数覆盖 */

// 业务函数，跑在 worker 线程里（不是主线程），所以打印的 tid 每次可能不一样。
// arg 是外面传进来的 &num[i]，转回 int* 再取值。
static void *process(void *arg)
{
    int id = *(int *)arg;

    printf("thread 0x%lx working on task %d\n", (unsigned long)pthread_self(), id);
    sleep(g_task_sec);                          /* 模拟业务处理耗时 */
    printf("task %d is end\n", id);

    return NULL;
}

int main(int argc, char *argv[])
{
    int task_num = 60;

    // 读命令行参数，不给就用默认值
    if (argc >= 2) task_num   = atoi(argv[1]);
    if (argc >= 3) g_task_sec = atoi(argv[2]);
    if (task_num   <= 0)     task_num   = 60;
    if (g_task_sec <= 0)     g_task_sec = 2;
    if (task_num   > TASK_MAX) task_num = TASK_MAX;

    // 造公司：最少 3 个工人，最多 100 个，任务队列最多排 100 个
    threadpool_t *thp = threadpool_create(3, 100, 100);
    if (thp == NULL) {
        printf("create pool fail\n");
        return -1;
    }
    printf("pool inited: min=3 max=100 queue=100, tasks=%d, task_sec=%d\n",
           task_num, g_task_sec);

    // 老板送货：传的是 &num[i]，不是 i。
    // num 是 main 的局部数组，活得比任务久，指针不会变野。
    int num[TASK_MAX];
    for (int i = 0; i < task_num; i++) {
        num[i] = i;
        printf("add task %d\n", i);
        threadpool_add(thp, process, (void *)&num[i]);
    }

    // 盯着看：每秒打一次 live/busy。live 涨了是扩容、跌了是缩容，busy 归零就是干完了。
    // 等的时间：理论干完 task_num*sec/3，再加两个巡检周期。
    int wait_s = task_num * g_task_sec / 3 + 2 * DEFAULT_TIME + 2;

    printf("---- 开始监视 %d 秒（Ctrl+C 可随时中断）----\n", wait_s);

    for (int t = 0; t < wait_s; t++) {
        printf("[monitor %2ds] live=%d busy=%d\n",
               t,
               threadpool_all_threadnum(thp),    /* 走 pool->lock，安全 */
               threadpool_busy_threadnum(thp));  /* 走 thread_counter，安全 */
        sleep(1);
    }

    // 拆公司：会 join 巡检线程，它可能正在 sleep(10)，所以最多卡 10 秒才退，正常。
    printf("---- destroy ----\n");
    threadpool_destroy(thp);

    printf("bye\n");
    return 0;
}
