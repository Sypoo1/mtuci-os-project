#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <time.h>

#define SERVER1_PORT 8001
#define SERVER2_PORT 8002
#define BUFFSIZE 4096

int connect_to_server(const char *host, int port) {
    int sockfd;
    struct sockaddr_in serv_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return -1;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", host);
        close(sockfd);
        return -1;
    }
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

void close_connection(int *sockfd) {
    if (*sockfd != -1) {
        close(*sockfd);
        *sockfd = -1;
    }
}

void send_command(int sockfd, const char *cmd) {
    char buf[BUFFSIZE];
    ssize_t n;
    // send command with newline
    snprintf(buf, sizeof(buf), "%s\n", cmd);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        perror("write");
        return;
    }
    // read response line
    n = read(sockfd, buf, sizeof(buf)-1);
    if (n <= 0) {
        perror("read");
        return;
    }
    buf[n] = '\0';
    printf("Response: %s", buf);
}

void menu() {
    printf("--- Client Menu ---\n");
    printf("1. Connect to Server 1\n");
    printf("2. Connect to Server 2\n");
    printf("3. Disconnect from Server 1\n");
    printf("4. Disconnect from Server 2\n");
    printf("5. Get resolution (Server 1)\n");
    printf("6. Get pixel color (Server 1)\n");
    printf("7. Get PID (Server 2)\n");
    printf("8. Get thread count (Server 2)\n");
    printf("9. Exit\n");
    printf("Choose an option: ");
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    if (argc > 1) host = argv[1];

    int sock1 = -1, sock2 = -1;
    int choice;
    char input[64];
    while (1) {
        menu();
        if (!fgets(input, sizeof(input), stdin)) break;
        choice = atoi(input);
        switch (choice) {
            case 1:
                if (sock1 == -1) {
                    sock1 = connect_to_server(host, SERVER1_PORT);
                    if (sock1 != -1)
                        printf("Connected to Server 1\n");
                } else {
                    printf("Already connected to Server 1\n");
                }
                break;
            case 2:
                if (sock2 == -1) {
                    sock2 = connect_to_server(host, SERVER2_PORT);
                    if (sock2 != -1)
                        printf("Connected to Server 2\n");
                } else {
                    printf("Already connected to Server 2\n");
                }
                break;
            case 3:
                close_connection(&sock1);
                printf("Disconnected from Server 1\n");
                break;
            case 4:
                close_connection(&sock2);
                printf("Disconnected from Server 2\n");
                break;
            case 5:
                if (sock1 != -1)
                    send_command(sock1, "GET_RESOLUTION");
                else
                    printf("Not connected to Server 1\n");
                break;
            case 6:
                if (sock1 != -1) {
                    int x, y;
                    printf("Enter x y: ");
                    if (scanf("%d %d", &x, &y) == 2) {
                        snprintf(input, sizeof(input), "GET_PIXEL %d %d", x, y);
                        send_command(sock1, input);
                        // consume leftover newline
                        getchar();
                    } else {
                        printf("Invalid input\n");
                        getchar();
                    }
                } else
                    printf("Not connected to Server 1\n");
                break;
            case 7:
                if (sock2 != -1)
                    send_command(sock2, "GET_PID");
                else
                    printf("Not connected to Server 2\n");
                break;
            case 8:
                if (sock2 != -1)
                    send_command(sock2, "GET_THREAD_COUNT");
                else
                    printf("Not connected to Server 2\n");
                break;
            case 9:
                close_connection(&sock1);
                close_connection(&sock2);
                printf("Exiting. Goodbye!\n");
                exit(EXIT_SUCCESS);
                break;
            default:
                printf("Invalid option\n");
                break;
        }
    }
    return 0;
}
