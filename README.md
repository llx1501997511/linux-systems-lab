# cpp-systems-lab

Linux 系统编程实践：从 TCP 套接字、epoll 事件驱动到动态线程池。

四个子工程独立编译、独立运行，每个都是可执行的最小完整实现。

| 目录 | 内容 | 行数 | 关键点 |
|---|---|---|---|
| `00-client` | TCP 客户端 | 89 | 连接、收发、EOF 退出 |
| `04-epoll-server` | epoll 回显服务器 | 123 | epoll 三件套、非阻塞 accept |
| `02-epoll-reactor` | epoll 反应堆 | 291 | 事件回调注册、部分发送、空闲超时断开 |
| `12-threadpool` | 动态线程池 | 291 | 环形队列、双锁分工、按积压扩缩容 |

环境：Ubuntu 22.04 / gcc 11.4 / glibc 2.35。全部代码 `-Wall -Wextra` 编译零告警。

## 编译与运行

每个目录自带 Makefile：

```bash
# 线程池：60 个任务，每个耗时 2 秒
cd 12-threadpool && make && ./test 60 2

# 反应堆服务器，另开终端连客户端
cd 02-epoll-reactor && make && ./server
cd 00-client && make && ./client 127.0.0.1 8000
```

## 12-threadpool

固定规模 worker + 一个巡检线程。

- 队列满时生产者阻塞，队列空时 worker 阻塞，两侧都用条件变量等待，不在锁上自旋
- `pool->lock` 保护任务队列和线程名册；`thread_counter` 单独保护 `busy_thr_num`，统计时不与入队出队争锁
- `threads[]` 当槽位池使用：worker 退出时把自己的槽位置 0，扩容时按 0 找空位复用，避免名册耗尽
- 巡检线程每 10 秒读一次积压和忙碌数：积压超过 10 个扩容，空闲数超过忙碌数两倍则缩容，单次变动上限 10 个

验证扩容缩容：

```
[monitor  0s] live=3  busy=0
[monitor 10s] live=13 busy=13   工头巡检，积压触发扩容
[monitor 20s] live=3  busy=0    空闲触发缩容
```

## 02-epoll-reactor

每个连接对应一个 `myevent_s`，通过 `epoll_event.data.ptr` 挂到 epoll 上，事件到达后按回调指针分发。

- 读回调读完数据后切换到写事件；写回调处理部分发送，未发完的部分挪到缓冲区头部，保持 EPOLLOUT 等待下次可写
- 使用水平触发（LT），没有开 EPOLLET。LT 下只要缓冲区还有数据就会再次通知，不需要一次读到 EAGAIN，牺牲一点性能换取不漏事件
- `eventadd` 根据 `status` 选择 `EPOLL_CTL_ADD` 还是 `MOD`，避免重复添加时撞上 EEXIST
- 每轮循环检查 `last_active`，超过 300 秒无活动的连接主动关闭

## 04-epoll-server

不封装回调结构，直接在 `epoll_wait` 之后按事件类型分支处理，是反应堆版本的对照实现。
用来对照理解：同样一套 epoll 接口，封装成回调模型之后代码如何组织。
