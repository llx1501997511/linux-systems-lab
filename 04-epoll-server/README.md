# epoll 回显服务器

最简形态的 epoll 服务器：不封装回调，`epoll_wait` 之后直接按事件类型分支处理。

## 编译运行

```bash
make
./server              # 默认监听 8000
```

配合 `00-client` 测试：

```bash
cd ../00-client && make && ./client 127.0.0.1 8000
```

## 与 02-epoll-reactor 的关系

这份是反应堆版本的对照实现。两者用同一套 epoll 接口，区别只在代码组织：

| | 本目录 | 02-epoll-reactor |
|---|---|---|
| 事件分发 | `epoll_wait` 后 if-else 分支 | 结构体内注册回调，`data.ptr` 取回后调用 |
| 状态保存 | 局部变量 | `myevent_s` 结构体，挂在 epoll 上 |
| 读写切换 | 每次 `epoll_ctl` 重新设置 | 回调指针 + `status` 决定 ADD/MOD |
| 适用规模 | 逻辑简单、事件类型少 | 连接多、事件类型多，需要解耦 |

先用这份把 epoll 三件套跑通，再换成回调模型，能看清「封装」到底封装掉了什么。

## 关键点

- `epoll_create` → `epoll_ctl(ADD)` 挂监听 fd → `epoll_wait` 循环
- 监听 fd 和连接 fd 都设非阻塞；`accept` 返回 EAGAIN 表示没有新连接，跳出循环
- 用 `&` 判断事件位，不能用 `==`：一个 fd 可能同时可读可写，
  `events` 是多个标志位或起来的值，用 `==` 会漏掉组合情况
