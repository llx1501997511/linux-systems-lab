# protobuf 序列化练习

跟着课把 protobuf 走了一遍：写 .proto、用 protoc 生成代码、填数据序列化再反序列化打印。

## 编译运行

```bash
make          # 会先跑 protoc 生成 msg.pb.cc/msg.pb.h，再编译
./demo
```

输出大概：

```
serialized size = 40 bytes
name=zhangsan id=101 email=zhangsan@example.com
phone[0]=13800000001
phone[1]=13800000002
```

## 记一下

- .proto 里字段后面那个数字是字段编号，不是顺序。编号会写进字节流，上线后不能乱改。
- 普通字段 set_xxx() / xxx()，repeated 用 add_xxx() / xxx(i) / xxx_size()。
- 机器上 protoc 是 3.12.4，不支持 optional，所以没写。
