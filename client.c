#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define S1 8001
#define S2 8002
#define BS 4096

int conn(const char *h, int p) {
    int sd = socket(AF_INET, SOCK_STREAM, 0), r;
    struct sockaddr_in a = {AF_INET, htons(p)};
    inet_pton(AF_INET, h, &a.sin_addr);
    if (connect(sd, (void *)&a, sizeof(a)) < 0)
        return -1;
    return sd;
}
void cl(int *s) {
    if (*s != -1)
        close(*s), *s = -1;
}
void cmd(int sd, const char *c) {
    char b[BS];
    if (sd < 0) {
        printf("Not connected\n");
        return;
    }
    snprintf(b, BS, "%s\n", c);
    write(sd, b, strlen(b));
    ssize_t n = read(sd, b, BS - 1);
    if (n > 0) {
        b[n] = 0;
        printf("%s", b);
    } else
        perror("read");
}
void menu() {
    puts("1.Connect1 2.Connect2 3.Dis1 4.Dis2 5.Res 6.Pixel 7.PID 8.Threads "
         "9.Exit");
    printf("> ");
}
int main(int a, char **v) {
    char *h = a > 1 ? v[1] : "127.0.0.1";
    int s1 = -1, s2 = -1;
    char in[64];
    while (1) {
        menu();
        if (!fgets(in, 64, stdin))
            break;
        int c = atoi(in);
        switch (c) {
        case1:
            if (s1 < 0 && (s1 = conn(h, S1)) >= 0)
                puts("OK1");
            break;
        case2:
            if (s2 < 0 && (s2 = conn(h, S2)) >= 0)
                puts("OK2");
            break;
        case3:
            cmd(s1, "DISCONNECT");
            cl(&s1);
            break;
        case4:
            cmd(s2, "DISCONNECT");
            cl(&s2);
            break;
        case5:
            cmd(s1, "GET_RESOLUTION");
            break;
        case6 : {
            int x, y;
            printf("x y: ");
            if (scanf("%d%d", &x, &y) == 2) {
                snprintf(in, 64, "GET_PIXEL %d %d", x, y);
                cmd(s1, in);
            }
            getchar();
        } break;
        case7:
            cmd(s2, "GET_PID");
            break;
        case8:
            cmd(s2, "GET_THREAD_COUNT");
            break;
        case9:
            cl(&s1);
            cl(&s2);
            exit(0);
        }
    }
}
