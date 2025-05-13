# Компилятор и флаги
CC       := gcc
CFLAGS   := -g -pthread
LDFLAGS  := -lX11

# Исходники и объекты
SRC_SRVR := server1.c server2.c
SRC_CLNT := client.c
SRC_MQ   := myqueue.c
HDRS     := myqueue.h
OBJS_SRVR := server1.o server2.o myqueue.o
OBJS_CLNT := client.o

# Файлы для форматирования
FORMAT_FILES := $(SRC_SRVR) $(SRC_CLNT) $(SRC_MQ) $(HDRS)

.PHONY: all format clean

# По умолчанию — форматируем и собираем всё
all: format server1 server2 client

# Форматирование исходников
format:
	@echo "Running clang-format..."
	clang-format -i --style=file:config.clang-format $(FORMAT_FILES)

# Сборка серверов и клиента
server1: server1.o myqueue.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server2: server2.o myqueue.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: client.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Правило компиляции .c → .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Очистка артефактов
clean:
	rm -f $(OBJS_SRVR) $(OBJS_CLNT) server1 server2 client
