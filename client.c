#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define S1_PORT 8001
#define S2_PORT 8002
#define BS 4096

// Читает одну строку (до '\n') из сокета и печатает её
void read_greeting(int sd) {
    char buf[BS];
    size_t total = 0;
    ssize_t n;
    while ((n = read(sd, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
        if (buf[total - 1] == '\n' || total >= sizeof(buf) - 1)
            break;
    }
    if (n < 0) {
        perror("read greeting");
        return;
    }
    buf[total] = '\0';
    printf("%s", buf);
}

int conn(const char *h, int p) {
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);

    if (inet_pton(AF_INET, h, &addr.sin_addr) != 1) {
        fprintf(stderr, "inet_pton: invalid address '%s'\n", h);
        close(sd);
        return -1;
    }

    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sd);
        return -1;
    }

    return sd;
}

void cl(int *s) {
    if (*s != -1) {
        close(*s);
        *s = -1;
    }
}

void cmd(int sd, const char *c) {
    if (sd < 0) {
        puts("Not connected");
        return;
    }

    char buf[BS];
    int cmd_len = snprintf(buf, sizeof(buf), "%s\n", c);
    if (write(sd, buf, cmd_len) != cmd_len) {
        perror("write");
        return;
    }

    // читаем ответ до первой '\n'
    size_t total = 0;
    ssize_t n;
    while ((n = read(sd, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
        if (buf[total - 1] == '\n' || total >= sizeof(buf) - 1)
            break;
    }
    if (n < 0) {
        perror("read");
        return;
    }
    buf[total] = '\0';
    printf("%s", buf);
}

void menu() {
    puts("\nMenu:");
    puts(" 1) Connect1");
    puts(" 2) Connect2");
    puts(" 3) Disconnect1");
    puts(" 4) Disconnect2");
    puts(" 5) GET_RESOLUTION");
    puts(" 6) GET_PIXEL");
    puts(" 7) GET_PID");
    puts(" 8) GET_THREAD_COUNT");
    puts(" 9) Exit");
    printf("> ");
}

int main(int argc, char **argv) {
    const char *host = (argc > 1 ? argv[1] : "127.0.0.1");
    int s1 = -1, s2 = -1;
    char line[64];

    while (1) {
        menu();

        if (!fgets(line, sizeof(line), stdin))
            break;

        int choice = atoi(line);
        switch (choice) {
        case 1:
            if (s1 < 0) {
                s1 = conn(host, S1_PORT);
                if (s1 >= 0) {
                    printf("Connected to port %d, server says:\n", S1_PORT);
                    read_greeting(s1);
                } else {
                    puts("Failed to connect1");
                }
            } else {
                puts("Already connected1");
            }
            break;

        case 2:
            if (s2 < 0) {
                s2 = conn(host, S2_PORT);
                if (s2 >= 0) {
                    printf("Connected to port %d, server says:\n", S2_PORT);
                    read_greeting(s2);
                } else {
                    puts("Failed to connect2");
                }
            } else {
                puts("Already connected2");
            }
            break;

        case 3:
            printf("Disconnected from port %d, server says:\n", S1_PORT);
            cmd(s1, "DISCONNECT");
            cl(&s1);
            break;

        case 4:
            printf("Disconnected from port %d, server says:\n", S2_PORT);
            cmd(s2, "DISCONNECT");
            cl(&s2);
            break;

        case 5:
            cmd(s1, "GET_RESOLUTION");
            break;

        case 6: {
            int x, y;
            printf("Enter x y: ");
            if (scanf("%d %d", &x, &y) == 2) {
                snprintf(line, sizeof(line), "GET_PIXEL %d %d", x, y);
                cmd(s1, line);
            } else {
                puts("Invalid input");
            }
            // очистка stdin после scanf
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF)
                ;
            break;
        }

        case 7:
            cmd(s2, "GET_PID");
            break;

        case 8:
            cmd(s2, "GET_THREAD_COUNT");
            break;

        case 9:
            cl(&s1);
            cl(&s2);
            puts("Exiting.");
            exit(0);

        default:
            puts("Unknown option");
        }
    }

    cl(&s1);
    cl(&s2);
    return 0;
}
