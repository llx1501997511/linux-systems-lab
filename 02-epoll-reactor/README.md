# epoll 反应堆

把每个连接的 fd、关注的事件、回调和缓冲区打包成一个结构体，
挂在 epoll 上，事件到达后按回调分发。

## 编译运行

```bash
make
./server                 # 默认监听 8000
```

另开终端：

```bash
nc 127.0.0.1 8000        # 输入什么，回什么的大写
```

## 结构

```c
struct myevent_s {
    int fd;                                      // 监听 fd 或连接 fd
    int events;                                  // 当前关注的事件
    void (*call_back)(int, int, void *);         // 事件到达时调谁
    void *arg;                                   // 指回自己
    int status;                                  // 是否已在 epoll 树上
    char buf[BUFLEN];
    int len;
    int last_active;
};
```

两个要点：

- `arg` 指向结构体自身，`epoll_event.data.ptr` 也存这个指针。`epoll_wait` 返回后直接
  从 `data.ptr` 拿到结构体，不需要额外的 fd → 结构体映射表。
- `status` 记录是否已经在树上。`eventadd` 靠它决定用 `EPOLL_CTL_ADD` 还是 `EPOLL_CTL_MOD`，
  重复 ADD 同一个 fd 会返回 EEXIST。

## 事件流转

```
acceptconn  ──新连接──> 注册 recvdata（EPOLLIN）
recvdata    ──读到数据──> 转大写，切成 senddata（EPOLLOUT）
senddata    ──发完──> 切回 recvdata（EPOLLIN）
            ──没发完──> 剩余数据挪到 buf 头部，保持 EPOLLOUT 等下次可写
```

切换事件时严格按 `eventdel → 改回调和 events → eventadd` 的顺序。
漏掉 `eventdel` 直接 ADD 会撞 EEXIST，因为 fd 还在树上。

## 部分发送

`send` 在内核发送缓冲区满时只发出一部分。处理方式是：

```c
int n = send(fd, ev->buf, ev->len, MSG_NOSIGNAL);
if (n > 0) {
    memmove(ev->buf, ev->buf + n, ev->len - n);
    ev->len -= n;
}
```

未发出的部分挪到缓冲区开头，长度递减，继续挂 EPOLLOUT 等待下一次可写。
不能直接丢弃，也不能假定一次 send 就发完。

`send` 必须带 `MSG_NOSIGNAL`。对端已经关闭时，不带这个标志会触发 SIGPIPE，
默认行为是终止进程，退出码 141，连后面的 printf 都执行不到。

## 触发模式

用的是水平触发（LT），没有设置 EPOLLET。

LT 下只要缓冲区还有数据，下一轮 `epoll_wait` 会继续返回该 fd，
所以一次 `recv` 没读干净也不会丢事件，代码不需要循环读到 EAGAIN。
代价是同一个 fd 可能被重复通知，多几次系统调用。

对正确性优先的场景，LT 是更稳的选择。

## 空闲连接回收

每轮循环遍历一遍事件数组，用 `time(NULL) - last_active` 和 `IDLE_TIMEOUT`（300 秒）比较，
超时的连接关掉并从树上摘除。

`last_active` 在每次 `eventset` 和每次读到数据时更新。
注意 `close` 会改写 `errno`，如果后续还要用 errno 判断错误类型，必须先把它存下来。

## 其他

- 监听 socket 和连接 socket 都设成非阻塞。`accept` 在非阻塞下返回 -1 且 errno 为 EAGAIN
  表示「暂时没有新连接了」，不是错误，应该跳出 accept 循环而不是报错退出。
- 所有 fd 都从同一个 `g_events[MAX_EVENTS + 1]` 数组里分配，`fd` 直接作为数组下标，
  所以数组大小要比 `MAX_EVENTS` 多 1。
