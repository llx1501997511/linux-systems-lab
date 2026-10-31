CC      = gcc
CFLAGS  = -Wall -Wextra -g -O0
TARGET  = client
all: $(TARGET)
$(TARGET): client.c
	$(CC) $(CFLAGS) client.c -o $(TARGET)
clean:
	rm -f $(TARGET)
.PHONY: all clean