# cpp-systems-lab

Linux 系统编程练习，跟着课敲的：TCP 客户端、epoll 服务器、epoll 反应堆、动态线程池、protobuf。

每个目录一个独立工程，自带 Makefile。

| 目录 | 内容 |
|---|---|
| 00-client | TCP 客户端 |
| 04-epoll-server | epoll 回显服务器（最简版） |
| 02-epoll-reactor | epoll 反应堆（回调结构体版） |
| 12-threadpool | 动态线程池 |
| 13-protobuf | protobuf 序列化练习 |

环境：Ubuntu 22.04 / gcc 11.4，`-Wall -Wextra` 编译无警告。

## 编译运行

```bash
# epoll 反应堆，另开终端连客户端
cd 02-epoll-reactor && make && ./server
cd 00-client && make && ./client 127.0.0.1 8000

# 线程池：60 个任务，每个 2 秒
cd 12-threadpool && make && ./test 60 2

# protobuf 序列化练习
cd 13-protobuf && make && ./demo
```
