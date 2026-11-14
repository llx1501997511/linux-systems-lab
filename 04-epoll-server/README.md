# epoll 回显服务器

最简单的 epoll 服务器：不封装回调结构，`epoll_wait` 之后直接按事件类型 if-else 分支。

## 编译运行

```bash
make
./server              # 默认 8000
```

配合 00-client：

```bash
cd ../00-client && make && ./client 127.0.0.1 8000
```

## 和 02-epoll-reactor 的区别

- 这个用 if-else 分事件，02 用结构体注册回调、`data.ptr` 反查。
- 先跑这个把 epoll 三件套（create / ctl / wait）捋清楚，再看 02 是怎么封装成回调模型的。

## 记一下

- epoll_create → epoll_ctl(ADD) 挂监听 fd → epoll_wait 循环。
- 监听和连接 fd 都设非阻塞；accept 返回 EAGAIN 表示没有新连接，跳出循环。
- 判断事件位用 `&` 不用 `==`，一个 fd 可能同时可读可写，events 是多个标志位或起来的。
