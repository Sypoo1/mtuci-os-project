CC = gcc
CFLAGS = -g
LDFLAGS = -lX11
BINS = server client
OBJS = server.o myqueue.o

all: $(BINS)

server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: client.o
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf *.dSYM $(BINS)
