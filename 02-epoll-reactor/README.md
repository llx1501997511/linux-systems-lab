# epoll 反应堆

把每个连接的 fd、事件、回调和缓冲区包成一个结构体，用 `data.ptr` 挂到 epoll 上，事件来了按回调分发。

## 编译运行

```bash
make
./server                 # 默认 8000
```

另开终端：

```bash
nc 127.0.0.1 8000        # 输入什么，回什么的大写
```

## 记一下

- 结构体存 fd、events、回调指针、arg、status、buf、len、last_active。arg 和 data.ptr 都指向结构体自己，epoll_wait 回来直接反查。
- status 记这个 fd 在不在树上，决定 eventadd 用 ADD 还是 MOD（重复 ADD 同一个 fd 会 EEXIST）。
- 切换读写事件按 `eventdel → 改回调和 events → eventadd` 的顺序，漏掉 del 直接 add 会撞 EEXIST。
- 部分发送：send 一次发不完的话，把剩下的 memmove 到 buf 头部，长度递减，继续挂 EPOLLOUT。
- send 必须带 MSG_NOSIGNAL。对端关了不带这个会 SIGPIPE，默认动作是杀进程（退出码 141）。
- 用的 LT 不是 ET，所以 recv 不用循环读到 EAGAIN，一次没读干净下轮还会通知。
- 每轮扫一遍 last_active，超过 300 秒没活动的连接关掉。
- 监听和连接 fd 都设非阻塞，accept 返回 EAGAIN 就是没新连接，跳出循环。
