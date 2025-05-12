/* server.c: single-instance + parent-child restart for Linux with graceful shutdown and SO_REUSEADDR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include <limits.h>

#include "myqueue.h"

#define SERVERPORT       8001
#define BUFFSIZE         4096
#define SOCKETERROR     -1
#define SERVERBACKLOG   100
#define THREAD_POOL_SIZE 20

static pid_t child_pid = 0;
static int server_socket = -1;
static int lock_fd = -1;

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;
typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void* handle_connection(void* p_client_socket);
int check(int exp, const char* msg);
void* thread_function(void* arg);
int server_loop(void);
bool ensure_single_instance(void);
void sigint_handler(int sig);
void child_sigterm_handler(int sig);

// ---------- single-instance (Linux) ----------
bool ensure_single_instance(void) {
    lock_fd = check(open("/tmp/my_server1.lock", O_CREAT | O_RDWR, 0666), "open lockfile");
    check(flock(lock_fd, LOCK_EX | LOCK_NB), "flock");
    return true;
}

// ---------- signal handler for graceful shutdown in parent ----------
void sigint_handler(int sig) {
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
    }
    if (server_socket != -1) close(server_socket);
    if (lock_fd != -1) close(lock_fd);
    unlink("/tmp/myserver1.lock");
    exit(EXIT_SUCCESS);
}

// ---------- child SIGTERM handler to exit cleanly ----------
void child_sigterm_handler(int sig) {
    if (server_socket != -1) close(server_socket);
    exit(EXIT_SUCCESS);
}

// ---------- main with parent-child supervisor (Linux) ----------
int main(int argc, char** argv) {
    // parent: install SIGINT handler, ignore in child
    signal(SIGINT, sigint_handler);
    // detect child mode
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        // child should handle SIGTERM to exit gracefully
        signal(SIGTERM, child_sigterm_handler);
        return server_loop();
    }
    // parent: single-instance check
    if (!ensure_single_instance()) return 1;
    // supervise child
    while (1) {
        pid_t pid = check(fork(), "fork");
        if (pid == 0) {
            execlp(argv[0], argv[0], "--child", NULL);
            perror("execlp");
            _exit(EXIT_FAILURE);
        }
        child_pid = pid;
        int status;
        check(waitpid(child_pid, &status, 0), "waitpid");
        fprintf(stderr, "Child exited (status %d), restarting in 1s...\n", WEXITSTATUS(status));
        sleep(1);
    }
    return 0;
}

// ---------- server loop with thread pool ----------
int server_loop(void) {
    int client_socket;
    SA_IN server_addr, client_addr;
    socklen_t addr_size;

    // start thread pool
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&thread_pool[i], NULL, thread_function, NULL);
    }

    server_socket = check(socket(AF_INET, SOCK_STREAM, 0), "socket");
    int opt = 1;
    check(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)), "setsockopt");

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVERPORT);
    check(bind(server_socket, (SA*)&server_addr, sizeof(server_addr)), "bind");
    check(listen(server_socket, SERVERBACKLOG), "listen");

    while (true) {
        printf("Waiting for connections...\n");
        addr_size = sizeof(server_addr);
        client_socket = check(accept(server_socket, (SA*)&client_addr, &addr_size), "accept");
        int* pclient = malloc(sizeof(int));
        *pclient = client_socket;
        pthread_mutex_lock(&mutex);
        enqueue(pclient);
        pthread_cond_signal(&condition_var);
        pthread_mutex_unlock(&mutex);
    }
    return 0;
}

// ---------- helpers ----------
int check(int exp, const char* msg) {
    if (exp == SOCKETERROR) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return exp;
}

void* thread_function(void* arg) {
    while (1) {
        int* pclient;
        pthread_mutex_lock(&mutex);
        while ((pclient = dequeue()) == NULL) {
            pthread_cond_wait(&condition_var, &mutex);
        }
        pthread_mutex_unlock(&mutex);

        handle_connection(pclient);
        free(pclient);
    }
    return NULL;
}

void* handle_connection(void* p)
{
    int client_socket = *(int*)p;
    char buffer[BUFFSIZE];
    ssize_t bytes_read;
    int msgsize = 0;
    char actualpath[PATH_MAX+1];

    // read filename
    while ((bytes_read = read(client_socket, buffer+msgsize, sizeof(buffer)-msgsize)) > 0) {
        msgsize += bytes_read;
        if (msgsize > BUFFSIZE-1 || buffer[msgsize-1] == '\n') break;
    }
    check((int)bytes_read, "recv");
    buffer[msgsize-1] = '\0';
    printf("REQUEST: %s\n", buffer);

    // resolve path
    if (realpath(buffer, actualpath) == NULL) {
        perror("realpath");
        close(client_socket);
        return NULL;
    }

    FILE* fp = fopen(actualpath, "r");
    if (!fp) {
        perror("fopen");
        close(client_socket);
        return NULL;
    }

    // send file contents
    while ((bytes_read = fread(buffer, 1, BUFFSIZE, fp)) > 0) {
        check((int)write(client_socket, buffer, bytes_read), "write");
    }

    close(client_socket);
    fclose(fp);
    return NULL;
}
