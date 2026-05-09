CC = gcc
CFLAGS = -Wall -D_POSIX_C_SOURCE=200809L

all: myinit

myinit: main.c
	$(CC) $(CFLAGS) -o myinit main.c

clean:
	rm -f myinit result.txt

.PHONY: all clean
