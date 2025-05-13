CC = gcc
CFLAGS = -g -pthread
LDFLAGS = -lX11

all: server1 server2 client

server1: server1.o myqueue.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server2: server2.o myqueue.o
	$(CC) $(CFLAGS) -o $@ $^

client: client.o
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o server1 server2 client