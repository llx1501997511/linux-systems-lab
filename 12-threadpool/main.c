/* ============================================================
 * main.c —— 线程池的"老板"：造池子 → 推任务 → 盯状态 → 拆池子
 *
 * 用法：
 *   ./test              默认 60 个任务，每个 2 秒
 *   ./test 60 2         60 个任务，每个 2 秒
 *   ./test 100 1        100 个任务，每个 1 秒（扩容更快看到）
 *
 * 为什么任务数要给够、单任务 sleep 要够长：
 *   工人只有 3 个，20 个 1 秒任务约 7 秒就干完，
 *   而工头每 DEFAULT_TIME(10) 秒才巡检一次。
 *   等它醒过来活早干完了 → 你永远看不到"扩容"。
 *   想让扩容出现：任务数 × 单任务秒数 ÷ 3 必须明显大于 10 秒。
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "threadpool.h"

#define TASK_MAX 128            /* 栈上数组的上限，防止参数给大了越界 */

static int g_task_sec = 2;      /* 单任务耗时（秒），可被命令行第二个参数覆盖 */

/* ============================================================
 * process —— 这就是"业务代码"
 *
 * 线程池只负责调度，具体干什么由这个函数决定。
 * 参数 arg 是外面传进来的 &num[i]，所以要 (int *) 还原再解引用。
 *
 * 注意：这个函数跑在 worker 线程里，不是主线程。
 *   所以打印出来的 tid 每次可能都不一样（同一个任务也可能换人接）。
 * ============================================================ */
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

    /* ---- 第 0 步：读命令行参数（不给就用默认值） ---- */
    if (argc >= 2) task_num   = atoi(argv[1]);
    if (argc >= 3) g_task_sec = atoi(argv[2]);
    if (task_num   <= 0)     task_num   = 60;
    if (g_task_sec <= 0)     g_task_sec = 2;
    if (task_num   > TASK_MAX) task_num = TASK_MAX;

    /* ---- 第 1 步：造公司 ----
     * 最少 3 个工人，最多 100 个，传送带最多排 100 个任务 */
    threadpool_t *thp = threadpool_create(3, 100, 100);
    if (thp == NULL) {
        printf("create pool fail\n");
        return -1;
    }
    printf("pool inited: min=3 max=100 queue=100, tasks=%d, task_sec=%d\n",
           task_num, g_task_sec);

    /* ---- 第 2 步：老板送货 ----
     * 注意传的是 &num[i]（地址），不是 i。
     * num 是 main 的栈变量，活得比所有任务都久，所以指针不会变野。
     * 如果传的是循环里的临时变量地址，所有任务会拿到同一个值 —— 经典 bug。 */
    int num[TASK_MAX];
    for (int i = 0; i < task_num; i++) {
        num[i] = i;
        printf("add task %d\n", i);
        threadpool_add(thp, process, (void *)&num[i]);
    }

    /* ---- 第 3 步：盯着看 ----
     * 每秒打印一次池子的两个关键数字：
     *   live —— 现在有几个工人（涨了 = 扩容，跌了 = 缩容）
     *   busy —— 有几个在干活（归零 = 活干完了）
     *
     * live / busy 走 threadpool.c 里的统计接口，内部各自加锁，
     * 所以这里直接调是安全的。
     *
     * 等多久？理论干完时间 task_num*sec/3，再加两个巡检周期：
     *   一轮可能触发扩容，另一轮才可能触发缩容。 */
    int wait_s = task_num * g_task_sec / 3 + 2 * DEFAULT_TIME + 2;

    printf("---- 开始监视 %d 秒（Ctrl+C 可随时中断）----\n", wait_s);

    for (int t = 0; t < wait_s; t++) {
        printf("[monitor %2ds] live=%d busy=%d\n",
               t,
               threadpool_all_threadnum(thp),    /* 走 pool->lock，安全 */
               threadpool_busy_threadnum(thp));  /* 走 thread_counter，安全 */
        sleep(1);
    }

    /* ---- 第 4 步：拆公司 ----
     * 顺序：挂电闸 → 等工头下班 → 广播叫醒所有工人 → 逐个 join → free
     *
     * destroy 里要 join 巡检线程，而它可能正睡在 sleep(DEFAULT_TIME) 里，
     *   所以这里会静默卡最多 10 秒才退出 —— 这是正常等待，不是死锁。 */
    printf("---- destroy ----\n");
    threadpool_destroy(thp);

    printf("bye\n");
    return 0;
}
