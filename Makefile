CC=gcc
CFLAGS=-w
SOURCES=main.c

all: server

server: $(SOURCES)
		$(CC) -o server $(SOURCES) $(CFLAGS)