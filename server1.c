#include "myqueue.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SERVERPORT 8001
#define BUFFSIZE 4096
#define SOCKETERROR -1
#define BACKLOG 100
#define THREADS 20

static pid_t child_pid = 0;
static int lock_fd = -1;
static int server_sock = -1;

pthread_t pool[THREADS];
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
typedef struct sockaddr_in SA_IN;

enum cmd { CONNECT, RES, PIXEL, DISCONNECT, UNKNOWN };

void *worker(void *arg);
bool ensure_single(void);
void parent_int(int sig);
void child_term(int sig);
void get_time(char *b, size_t n);
void get_resolution(char *b);
void get_pixel(int x, int y, char *b);

bool ensure_single(void) {
    lock_fd = open("/tmp/server1.lock", O_CREAT | O_RDWR, 0666);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0)
        return false;
    return true;
}

void parent_int(int sig) {
    if (child_pid > 0)
        kill(child_pid, SIGTERM);
    if (server_sock != -1)
        close(server_sock);
    if (lock_fd != -1)
        close(lock_fd);
    unlink("/tmp/server1.lock");
    exit(0);
}

void child_term(int sig) {
    if (server_sock != -1)
        close(server_sock);
    exit(0);
}

int main() {
    signal(SIGINT, parent_int);
    if (!ensure_single()) {
        fprintf(stderr, "Already running\n");
        return 1;
    }
    while (1) {
        pid_t pid = fork();
        if (pid < 0)
            exit(1);
        if (pid == 0) {
            signal(SIGTERM, child_term);
            goto child;
        }
        child_pid = pid;
        int st;
        waitpid(pid, &st, 0);
        fprintf(stderr, "Child exited, respawning...\n");
        sleep(1);
    }
child:
    for (int i = 0; i < THREADS; i++)
        pthread_create(&pool[i], 0, worker, 0);
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    SA_IN addr = {.sin_family = AF_INET,
                  .sin_addr.s_addr = INADDR_ANY,
                  .sin_port = htons(SERVERPORT)};
    bind(server_sock, (SA *)&addr, sizeof(addr));
    listen(server_sock, BACKLOG);
    while (1) {
        int c = accept(server_sock, NULL, NULL);
        int *p = malloc(sizeof(int));
        *p = c;
        pthread_mutex_lock(&mtx);
        enqueue(p);
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&mtx);
    }
}

void *worker(void *arg) {
    while (1) {
        int *ps;
        pthread_mutex_lock(&mtx);
        while ((ps = dequeue()) == NULL)
            pthread_cond_wait(&cv, &mtx);
        pthread_mutex_unlock(&mtx);
        int sock = *ps;
        free(ps);
        char in[BUFFSIZE], out[BUFFSIZE];
        ssize_t r;
        while ((r = read(sock, in, 1)) > 0) { // read line
            static char buf[BUFFSIZE];
            static size_t pos;
            if (in[0] == '\n' || pos + 1 >= BUFFSIZE) {
                buf[pos] = 0;
                pos = 0;
            } else
                buf[pos++] = in[0];
            if (in[0] != '\n')
                continue;
            char ts[64];
            get_time(ts, sizeof(ts));
            int x, y;
            if (strcmp(buf, "CONNECT") == 0)
                snprintf(out, sizeof(out), "%s OK Connected\n", ts);
            else if (strcmp(buf, "GET_RESOLUTION") == 0) {
                char rbuf[64];
                get_resolution(rbuf);
                snprintf(out, sizeof(out), "%s OK %s\n", ts, rbuf);
            } else if (sscanf(buf, "GET_PIXEL %d %d", &x, &y) == 2) {
                char cbuf[32];
                get_pixel(x, y, cbuf);
                snprintf(out, sizeof(out), "%s OK %s\n", ts, cbuf);
            } else if (strcmp(buf, "DISCONNECT") == 0) {
                snprintf(out, sizeof(out), "%s OK Disconnected\n", ts);
                write(sock, out, strlen(out));
                break;
            } else
                snprintf(out, sizeof(out), "%s ERROR Unknown\n", ts);
            write(sock, out, strlen(out));
        }
        close(sock);
    }
    return NULL;
}

void get_time(char *b, size_t n) {
    time_t t = time(NULL);
    struct tm m;
    localtime_r(&t, &m);
    strftime(b, n, "%Y-%m-%d %H:%M:%S", &m);
}
void get_resolution(char *b) {
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        strcpy(b, "Error");
        return;
    }
    Screen *s = DefaultScreenOfDisplay(d);
    sprintf(b, "%dx%d", s->width, s->height);
    XCloseDisplay(d);
}
void get_pixel(int x, int y, char *b) {
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        strcpy(b, "Error");
        return;
    }
    Window r = RootWindow(d, DefaultScreen(d));
    XImage *i = XGetImage(d, r, x, y, 1, 1, AllPlanes, ZPixmap);
    unsigned long p = XGetPixel(i, 0, 0);
    XFree(i);
    int R = (p & i->red_mask) >> 16, G = (p & i->green_mask) >> 8,
        B = p & i->blue_mask;
    sprintf(b, "#%02X%02X%02X", R, G, B);
    XCloseDisplay(d);
}