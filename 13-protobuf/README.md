# Protobuf 序列化

用 Protocol Buffers 打通「填数据 → 序列化成字节 → 反序列化还原」的完整链路，作为 MiniDDS 消息序列化方案的前置实验。

## 文件

| 文件 | 内容 |
|---|---|
| `msg.proto` | 消息定义（`Person`，含普通字段和 `repeated` 字段） |
| `demo.cc` | 测试程序：填数据、序列化、反序列化、读回打印 |
| `msg.pb.cc` / `msg.pb.h` | 由 `protoc` 生成，不入库（`.gitignore` 排除） |

## 编译运行

```bash
# 依赖：protobuf-compiler libprotobuf-dev
make          # 会自动先跑 protoc 生成代码，再编译
./demo
```

输出示例：

```
serialized size = 40 bytes
name=zhangsan id=101 email=zhangsan@example.com
phone[0]=13800000001
phone[1]=13800000002
```

## 关键点

**`.proto` → 生成的类 → 自己代码**，三段分工：

- `.proto` 描述消息结构（字段类型 + 字段名 + 唯一编号）
- `protoc --cpp_out=. msg.proto` 生成 `msg.pb.h`（声明）和 `msg.pb.cc`（实现）
- 自己的代码 `#include "msg.pb.h"` 拿到声明，编译时把 `msg.pb.cc` 一起编进去拿实现，链接 `-lprotobuf`

**字段编号是字段的身份，不是序号。** 序列化后编号写进字节流，上线后不可改动、不可复用，否则老客户端的数据会被解析错。

**普通字段和 `repeated` 字段的 API 不同**：普通字段是 `set_name()` / `name()`；`repeated`（数组）是 `add_phone()` / `phone(i)` / `phone_size()`。

**体积优势**：字段名不存进字节流，用字段编号（一个字节的 tag）代替；数字用变长编码。实测 `Person{name,id,email,两个电话}` 序列化后 60 字节，比 JSON 文本小。

## 环境

`protoc 3.12.4`。proto3 的 `optional` 关键字要 3.15 才支持，当前版本不写 `optional`——普通字段可缺省，`repeated` 表示数组，MiniDDS 的消息用不到 `optional`。
