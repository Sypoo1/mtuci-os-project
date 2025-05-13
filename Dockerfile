# Используем минимальный образ с компилятором C
FROM alpine:latest

# Устанавливаем компилятор и прочее
RUN apk add --no-cache build-base

# Копируем исходный код
COPY client.c /app/client.c

# Сборка приложения
RUN gcc /app/client.c -o /app/client

# Указываем рабочую директорию
WORKDIR /app

# Запускаем приложение
ENTRYPOINT ["./client"]
