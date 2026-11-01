# protoc 生成 msg.pb.cc/msg.pb.h，再一起编译
CXX      = g++
CXXFLAGS = -Wall -g -O0
LDLIBS   = -lprotobuf

demo: demo.o msg.pb.o
	$(CXX) $^ -o $@ $(LDLIBS)

demo.o: demo.cc msg.pb.h
	$(CXX) -c demo.cc $(CXXFLAGS)

msg.pb.o: msg.pb.cc msg.pb.h
	$(CXX) -c msg.pb.cc $(CXXFLAGS)

msg.pb.cc msg.pb.h: msg.proto
	protoc --cpp_out=. msg.proto

clean:
	rm -f *.o demo msg.pb.cc msg.pb.h

.PHONY: clean
