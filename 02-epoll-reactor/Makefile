CC      = gcc
CFLAGS  = -Wall -Wextra -g -O0
TARGET  = server
all: $(TARGET)
$(TARGET): server.c
	$(CC) $(CFLAGS) server.c -o $(TARGET)
clean:
	rm -f $(TARGET)
.PHONY: all clean