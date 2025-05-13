#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

void *receive_handler(void *arg) {
    int sock = *(int *)arg;
    char buffer[BUFFER_SIZE];
    while (1) {
        ssize_t bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("Сервер отключился\n");
            exit(0);
        }
        buffer[bytes_received] = '\0';
        printf("Обновление: %s\n", buffer);
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8001);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
        perror("Ошибка подключения");
        exit(1);
    }

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, receive_handler, &sock);

    printf("Команды:\n");
    printf("GET resolution - разрешение экрана\n");
    printf("GET pixel_color X Y - цвет пикселя\n");
    printf("DISCONNECT - отключиться\n");

    char command[BUFFER_SIZE];
    while (1) {
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "DISCONNECT") == 0) {
            send(sock, command, strlen(command), 0);
            break;
        }

        if (strncmp(command, "GET ", 4) == 0) {
            send(sock, command, strlen(command), 0);
        } else {
            printf("Неизвестная команда\n");
        }
    }

    close(sock);
    return 0;
}