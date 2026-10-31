# 编译：三个文件一起编，-pthread 必须有（不能只在链接时加 -lpthread）
CC      = gcc
CFLAGS  = -Wall -g -pthread -O0

test: threadpool.o main.o
	$(CC) $^ -o $@ $(CFLAGS)

threadpool.o: threadpool.c threadpool.h
	$(CC) -c threadpool.c $(CFLAGS)

main.o: main.c threadpool.h
	$(CC) -c main.c $(CFLAGS)

clean:
	rm -f *.o test

.PHONY: clean
