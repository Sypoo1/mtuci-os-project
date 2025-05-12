CC=gcc
CFLAGS=-g
BINS=server
OBJS=server.o myqueue.o

all: $(BINS)

server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm -rf *.dSYM $(BINS)