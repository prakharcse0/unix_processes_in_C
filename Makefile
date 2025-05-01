CC = g++
CFLAGS = -Wall -Wextra -std=c++11

all: tiny_shell

tiny_shell: tiny_shell.cpp
	$(CC) $(CFLAGS) -o tiny_shell tiny_shell.cpp

clean:
	rm -f tiny_shell

.PHONY: all clean