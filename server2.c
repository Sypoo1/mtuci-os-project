#include "myqueue.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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

#define SERVERPORT 8002
#define BUFFSIZE 4096
#define BACKLOG 100
#define THREADS 20

static pid_t child_pid2 = 0;
static int lf2 = -1, ss2 = -1;
pthread_t pool2[THREADS];
pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv2 = PTHREAD_COND_INITIALIZER;

enum c2 { CON2, PID, TC, DIS2, UN2 };
void *wrk2(void *);
bool single2(void);
void pi2(int sig);
void ct2(int sig);
void gt(char *b, size_t n);

bool single2() {
    lf2 = open("/tmp/server2.lock", O_CREAT | O_RDWR, 0666);
    if (lf2 < 0 || flock(lf2, LOCK_EX | LOCK_NB) < 0)
        return false;
    return true;
}
void pi2(int s) {
    if (child_pid2 > 0)
        kill(child_pid2, SIGTERM);
    if (ss2 != -1)
        close(ss2);
    if (lf2 != -1)
        close(lf2);
    unlink("/tmp/server2.lock");
    exit(0);
}
void ct2(int s) {
    if (ss2 != -1)
        close(ss2);
    exit(0);
}
int main() {
    signal(SIGINT, pi2);
    if (!single2()) {
        fprintf(stderr, "Already2\n");
        return 1;
    }
    while (1) {
        pid_t p = fork();
        if (p < 0)
            exit(1);
        if (p == 0) {
            signal(SIGTERM, ct2);
            goto ch2;
        }
        child_pid2 = p;
        waitpid(p, NULL, 0);
        sleep(1);
    }
ch2:
    for (int i = 0; i < THREADS; i++)
        pthread_create(&pool2[i], 0, wrk2, 0);
    ss2 = socket(AF_INET, SOCK_STREAM, 0);
    int o = 1;
    setsockopt(ss2, SOL_SOCKET, SO_REUSEADDR, &o, sizeof(o));
    struct sockaddr_in a = {AF_INET, htons(SERVERPORT), INADDR_ANY};
    bind(ss2, (struct sockaddr *)&a, sizeof(a));
    listen(ss2, BACKLOG);
    while (1) {
        int c = accept(ss2, NULL, NULL);
        int *p = malloc(sizeof(int));
        *p = c;
        pthread_mutex_lock(&m2);
        enqueue(p);
        pthread_cond_signal(&cv2);
        pthread_mutex_unlock(&m2);
    }
}

void *wrk2(void *) {
    while (1) {
        int *ps;
        pthread_mutex_lock(&m2);
        while ((ps = dequeue()) == NULL)
            pthread_cond_wait(&cv2, &m2);
        pthread_mutex_unlock(&m2);
        int s = *ps;
        free(ps);
        char in[BUFFSIZE], out[BUFFSIZE];
        ssize_t r;
        while ((r = read(s, in, 1)) > 0) {
            static char buf[BUFFSIZE];
            static size_t pos;
            if (in[0] == '\n' || pos + 1 >= BUFFSIZE) {
                buf[pos] = 0;
                pos = 0;
            } else
                buf[pos++] = in[0];
            char ts[64];
            gt(ts, sizeof(ts));
            if (strcmp(buf, "CONNECT") == 0)
                snprintf(out, sizeof(out), "%s OK Connected\n", ts);
            else if (strcmp(buf, "GET_PID") == 0)
                snprintf(out, sizeof(out), "%s OK %d\n", ts, (int)getpid());
            else if (strcmp(buf, "GET_THREAD_COUNT") == 0)
                snprintf(out, sizeof(out), "%s OK %d\n", ts, THREADS);
            else if (strcmp(buf, "DISCONNECT") == 0) {
                snprintf(out, sizeof(out), "%s OK Disconnected\n", ts);
                write(s, out, strlen(out));
                break;
            } else
                snprintf(out, sizeof(out), "%s ERROR Unknown\n", ts);
            write(s, out, strlen(out));
        }
        close(s);
    }
    return NULL;
}

void gt(char *b, size_t n) {
    time_t t = time(NULL);
    struct tm m;
    localtime_r(&t, &m);
    strftime(b, n, "%Y-%m-%d %H:%M:%S", &m);
}
