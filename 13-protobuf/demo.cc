// 序列化 / 反序列化的完整链路：填数据 -> 序列化成字节 -> 反序列化还原。
#include <iostream>
#include <string>
#include "msg.pb.h"

int main() {
    Person p;
    p.set_name("zhangsan");
    p.set_id(101);
    p.set_email("zhangsan@example.com");
    p.add_phone("13800000001");
    p.add_phone("13800000002");

    std::string bytes;
    p.SerializeToString(&bytes);
    std::cout << "serialized size = " << bytes.size() << " bytes" << std::endl;

    Person q;
    q.ParseFromString(bytes);

    std::cout << "name=" << q.name()
              << " id=" << q.id()
              << " email=" << q.email() << std::endl;
    for (int i = 0; i < q.phone_size(); ++i) {
        std::cout << "phone[" << i << "]=" << q.phone(i) << std::endl;
    }

    return 0;
}
