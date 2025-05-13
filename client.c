#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8001
#define BUFFSIZE 4096

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file-path-on-server>\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr = {.sin_family = AF_INET,
                                    .sin_port = htons(SERVER_PORT)};
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    // Send the filename + newline
    char request[BUFFSIZE];
    snprintf(request, sizeof(request), "%s\n", argv[1]);
    if (write(sock, request, strlen(request)) < 0) {
        perror("write");
        close(sock);
        return 1;
    }

    // Read back the file contents
    ssize_t n;
    char buffer[BUFFSIZE];
    while ((n = read(sock, buffer, sizeof(buffer))) > 0) {
        fwrite(buffer, 1, n, stdout);
    }
    if (n < 0)
        perror("read");

    close(sock);
    return 0;
}
