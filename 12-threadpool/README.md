# 动态线程池

固定最小规模的 worker，配一个巡检线程按负载自动增减。

## 文件

| 文件 | 内容 |
|---|---|
| `threadpool.h` | 对外接口与结构体定义 |
| `threadpool.c` | 实现 |
| `main.c` | 测试程序：推任务、每秒打印线程数与忙碌数 |

## 编译运行

```bash
make
./test 60 2      # 60 个任务，每个耗时 2 秒
./test 100 1     # 100 个任务，每个 1 秒
```

参数：第一个是任务数，第二个是单任务秒数。不给参数默认 60 / 2。

## 结构

```
          add()                     worker 池
  主线程 -------> 环形队列  <------  min_thr_num 个线程
                     ^
                     |  每 10 秒巡检一次
                 巡检线程：积压就扩容，空闲就缩容
```

内部有两把锁：

- `pool->lock`：保护任务队列（front/rear/size）、`live_thr_num`、`threads[]` 名册、`wait_exit_thr_num`
- `pool->thread_counter`：只保护 `busy_thr_num`

分成两把锁的原因是忙碌计数在每个任务前后各改一次，频次比入队出队还高，
如果和队列共用一把锁，所有 worker 会在同一个临界区上排队。

## 关键设计

**队列满/空都在条件变量上等。**
`add` 在队列满时 `wait(queue_not_full)`，worker 在队列空时 `wait(queue_not_empty)`，
出队后 `signal(queue_not_full)`，入队后 `signal(queue_not_empty)`。全程没有轮询。

**threads[] 是槽位池。**
worker 主动退出时把自己的槽位置 0。不清的话，这个位置会永久持有一个已经消失的 tid，
而扩容靠 `threads[i] == 0` 找空位，名册只增不减，反复扩缩后即便没到上限也招不进新人。

**扩缩容判据。**

```c
// 扩容：积压超过阈值，且还没到上限
if (queue_size >= MIN_WAIT_TASK_NUM && live_num < pool->max_thr_num)

// 缩容：空闲人数超过忙碌人数两倍，且还高于最小值
if (busy_num * 2 < live_num && live_num > pool->min_thr_num)
```

缩容名额按实际余量计算 `num = live_thr_num - min_thr_num`，不写死。
写死会出现「名额悬空」：实际只能裁 2 个却设了 10 个，剩下 8 个名额
会在下一次任务到来时把本该干活的 worker 裁掉。

缩容用 `broadcast` 唤醒全部 worker，由 `wait_exit_thr_num` 这个共享计数器控制实际退出人数，
抢完名额自然归零，多醒的线程回到等待状态，不会多裁。

**destroy 的顺序。**

```
挂 shutdown 标志 → join 巡检线程 → broadcast 唤醒全部 worker
    → 按 max_thr_num 遍历名册逐个 join → 释放内存
```

遍历名册必须用 `max_thr_num`（容量，常量）而不是 `live_thr_num`（人数，会变）。
名册按槽位排列，中途退出会留下空洞，按人数扫前 N 格会漏掉排在后面的活线程，
`free(pool)` 之后那些线程还在访问已释放的锁，就是 use-after-free。
空槽要跳过，对 tid 为 0 的槽 join 会返回 ESRCH。

## 验证

跑 `./test 60 2`，每秒打印一行：

```
[monitor  0s] live=3  busy=0     3 个 worker 就绪
[monitor  1s] live=3  busy=3     全部投入，队列积压 57 个
[monitor 10s] live=13 busy=13    巡检触发扩容
[monitor 18s] live=13 busy=0     任务全部完成
[monitor 20s] live=3  busy=0     空闲触发缩容
```

`live` 从 3 到 13 再到 3，说明扩容和缩容双向生效。

想验证「扩容由积压触发而不是任务数触发」，跑 `./test 20 1`：
20 个 1 秒任务 7 秒就跑完，巡检线程 10 秒才醒，醒来时队列已空，`live` 全程停在 3。

## 已知简化

- 缩容名额是「先到先得」的：如果缩容后队列立刻来任务，`add` 的 signal 会唤醒一个 worker，
  而它醒来后先判断裁员条件，可能直接辞职，出现「有活干却在裁人」。
  修法是让 worker 辞职前复查 `queue_size == 0`。当前版本没做，影响是偶发少几个线程、下一轮巡检再补回来。
- 没有做任务优先级，队列是纯 FIFO。
