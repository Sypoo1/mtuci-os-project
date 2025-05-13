#include "myqueue.h"
#include <X11/Xlib.h>
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

#define SERVERPORT       8001
#define BUFFSIZE         4096
#define SOCKETERROR     -1
#define SERVERBACKLOG   100
#define THREAD_POOL_SIZE 20

static pid_t child_pid = 0;
static int lock_fd = -1;
static int server_socket = -1;

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;
typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void *handle_connection(void *p_client_socket);
int check(int exp, const char *msg);
void *thread_function(void *arg);
int server_loop(void);
bool ensure_single_instance(void);
void parent_sigint(int sig);
void child_sigterm(int sig);
void get_timestamp(char *buf, size_t len);
void get_resolution(char *buf);
void get_pixel_color(int x, int y, char *buf);

// Ensure only one instance via lock file (parent only)
bool ensure_single_instance(void) {
    lock_fd = check(open("/tmp/my_server1.lock", O_CREAT | O_RDWR, 0666), "open lockfile");
    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        perror("flock");
        close(lock_fd);
        return false;
    }
    return true;
}

// Parent SIGINT: stop child, cleanup
void parent_sigint(int sig) {
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
    }
    if (server_socket != -1) close(server_socket);
    if (lock_fd != -1) close(lock_fd);
    unlink("/tmp/my_server1.lock");
    exit(EXIT_SUCCESS);
}

// Child SIGTERM: exit gracefully
void child_sigterm(int sig) {
    if (server_socket != -1) close(server_socket);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, parent_sigint);

    // Child mode
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        signal(SIGTERM, child_sigterm);
        return server_loop();
    }

    // Parent mode
    if (!ensure_single_instance()) {
        fprintf(stderr, "Another instance is running.\n");
        return 1;
    }

    while (1) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            execlp(argv[0], argv[0], "--child", NULL);
            perror("execlp");
            _exit(EXIT_FAILURE);
        }
        child_pid = pid;
        int status;
        waitpid(child_pid, &status, 0);
        fprintf(stderr, "Child exited (status %d), restarting in 1s...\n", WEXITSTATUS(status));
        sleep(1);
    }
    return 0;
}

int server_loop(void) {
    int client_socket, addr_size;
    SA_IN server_addr, client_addr;

    // create a banch of threads to handle connections
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&thread_pool[i], NULL, thread_function, NULL);
    }

    check((server_socket = socket(AF_INET, SOCK_STREAM, 0)),
          "Failed to create a socket");

    // printf("server_socket=%d\n",server_socket);

    // Set SO_REUSEADDR option
    int opt = 1;
    check(
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)),
        "setsockopt(SO_REUSEADDR) failed");

    // initialize the address struct
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVERPORT);

    check(bind(server_socket, (SA *)&server_addr, sizeof(server_addr)),
          "Bind Failed!");
    check(listen(server_socket, SERVERBACKLOG), "Listen Failed!");
    // printf("server_socket=%d\n",server_socket);
    while (true) {

        printf("Waiting for connections...\n");
        // wait for, and eventually accept any incoming connections
        addr_size = sizeof(SA_IN);
        check(client_socket = accept(server_socket, (SA *)&client_addr,
                                     (socklen_t *)&addr_size),
              "accept failed");
        printf("Connected!\n");

        int *pclient = malloc(sizeof(int));
        *pclient = client_socket;

        // make sure only one thread messes with queue
        pthread_mutex_lock(&mutex);
        enqueue(pclient);
        pthread_cond_signal(&condition_var);
        pthread_mutex_unlock(&mutex);
    }
    return 0;
}

int check(int exp, const char *msg) {
    if (exp == SOCKETERROR) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return exp;
}

void *thread_function(void *arg) {
    while (1) {
        int *pclient;
        pthread_mutex_lock(&mutex);
        if ((pclient = dequeue()) == NULL) {
            pthread_cond_wait(&condition_var, &mutex);
        }
        pthread_mutex_unlock(&mutex);
        handle_connection(pclient);
    }
}

void *handle_connection(void *p_client_socket) {
    int client_socket = *(int *)p_client_socket;
    free(p_client_socket);

    char buffer[BUFFSIZE];
    ssize_t n = read(client_socket, buffer, BUFFSIZE - 1);
    if (n <= 0) { close(client_socket); return NULL; }
    buffer[n] = '\0';
    char *nl = strchr(buffer, '\n'); if (nl) *nl = '\0';

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    if (strcmp(buffer, "CONNECT") == 0) {
        dprintf(client_socket, "OK CONNECT %s\n", timestamp);
    } else if (strcmp(buffer, "GET resolution") == 0) {
        char res[32];
        get_resolution(res);
        dprintf(client_socket, "RESOLUTION %s %s\n", res, timestamp);
    } else if (strncmp(buffer, "GET pixel_color", 15) == 0) {
        int x, y;
        if (sscanf(buffer, "GET pixel_color %d %d", &x, &y) == 2) {
            char col[16];
            get_pixel_color(x, y, col);
            dprintf(client_socket, "PIXEL_COLOR %d %d %s %s\n", x, y, col, timestamp);
        } else {
            dprintf(client_socket, "ERROR bad command %s\n", timestamp);
        }
    } else if (strcmp(buffer, "DISCONNECT") == 0) {
        dprintf(client_socket, "OK DISCONNECT %s\n", timestamp);
    } else {
        dprintf(client_socket, "ERROR unknown %s\n", timestamp);
    }

    close(client_socket);
    return NULL;
}

void get_timestamp(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm);
}

void get_resolution(char *buf) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { strcpy(buf, "Error"); return; }
    Screen *s = DefaultScreenOfDisplay(d);
    sprintf(buf, "%dx%d", s->width, s->height);
    XCloseDisplay(d);
}

void get_pixel_color(int x, int y, char *buf) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { strcpy(buf, "Error"); return; }
    Window root = RootWindow(d, DefaultScreen(d));
    XImage *img = XGetImage(d, root, x, y, 1, 1, AllPlanes, ZPixmap);
    if (!img) { strcpy(buf, "Error"); XCloseDisplay(d); return; }
    unsigned long pix = XGetPixel(img, 0, 0);
    XFree(img);
    int r = (pix & img->red_mask) >> 16;
    int g = (pix & img->green_mask) >> 8;
    int b = pix & img->blue_mask;
    sprintf(buf, "#%02X%02X%02X", r, g, b);
    XCloseDisplay(d);
}
