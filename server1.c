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
#include <unistd.h>

#include "myqueue.h"

#define SERVERPORT 8001
#define BUFFSIZE 4096
#define SOCKETERROR -1
#define SERVERBACKLOG 100
#define THREAD_POOL_SIZE 20

static pid_t child_pid = 0;
static int lock_fd = -1;
static int server_socket = -1;

static Display *g_display = NULL;
static Window g_rootwin = 0;

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
static void get_timestamp(char *out, size_t len);
void get_screen_resolution(int *width, int *height);
int get_pixel_color(int x, int y, int *r, int *g, int *b);

// Ensure only one instance via lock file (parent only)
bool ensure_single_instance(void) {
    // printf("ensure_single_instance()\n");
    lock_fd = check(open("/tmp/my_server1.lock", O_CREAT | O_RDWR, 0666),
                    "open lockfile");
    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        perror("flock");
        close(lock_fd);
        return false;
    }
    // printf("ensure_single_instance return true\n");
    return true;
}

// Parent SIGINT: stop child, cleanup
void parent_sigint(int sig) {
    // printf("parent_sigint() wit sig=%d\n", sig);
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
    }
    if (server_socket != -1)
        close(server_socket);
    if (lock_fd != -1)
        close(lock_fd);
    unlink("/tmp/my_server1.lock");
    exit(EXIT_SUCCESS);
}

// Child SIGTERM: exit gracefully
void child_sigterm(int sig) {
    // printf("child_sigterm() with sig=%d\n", sig);
    if (server_socket != -1)
        close(server_socket);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {

    if (!XInitThreads()) {

        fprintf(stderr, "XInitThreads failed\n");
        return EXIT_FAILURE;
    }
    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        fprintf(stderr, "Cannot open display\n");
        return EXIT_FAILURE;
    }
    int screen = DefaultScreen(g_display);
    g_rootwin = XRootWindow(g_display, screen);

    // printf("main() with %d arguments:\n", argc);
    // for(int i = 0; i < argc; i++) {
    //     printf("argument %d == %s\n", i+1, argv[i]);
    // }
    // printf("\n");
    signal(SIGINT, parent_sigint);

    // Child mode
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        // printf("Child mode, server_socket=%d\n", server_socket);
        signal(SIGTERM, child_sigterm);
        return server_loop();
    }

    // Parent mode
    if (!ensure_single_instance()) {
        // printf("Parent mode\n");
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
            // printf("child process with pid=%d and pid=%d\n",pid, getpid());
            execlp(argv[0], argv[0], "--child", NULL);
            perror("execlp");
            _exit(EXIT_FAILURE);
        }
        child_pid = pid;
        int status;
        waitpid(child_pid, &status, 0);
        fprintf(stderr, "Child exited (status %d), restarting in 1s...\n",
                WEXITSTATUS(status));
        sleep(1);
    }

    XCloseDisplay(g_display);

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
    printf("Ready to accept connections!\n");
    while (true) {

        // wait for, and eventually accept any incoming connections
        addr_size = sizeof(SA_IN);
        check(client_socket = accept(server_socket, (SA *)&client_addr,
                                     (socklen_t *)&addr_size),
              "accept failed");

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

    while (true) {
        int *pclient;
        pthread_mutex_lock(&mutex);
        if ((pclient = dequeue()) == NULL) {
            pthread_cond_wait(&condition_var, &mutex);
            // try again
            pclient = dequeue();
        }
        pthread_mutex_unlock(&mutex);

        handle_connection(pclient);
    }
}

void *handle_connection(void *p_client_socket) {
    int client_socket = *(int *)p_client_socket;
    free(p_client_socket);

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_socket, (struct sockaddr *)&client_addr, &addr_len);

    char *client_ip = inet_ntoa(client_addr.sin_addr);
    int client_port = ntohs(client_addr.sin_port);

    printf("Client connected: %s:%d\n", client_ip, client_port);

    char buffer[BUFFSIZE];
    char reply[BUFFSIZE];
    ssize_t n;

    // Сразу отправляем OK с текущим временем
    {
        char ts[64];
        get_timestamp(ts, sizeof(ts));
        int len =
            snprintf(reply, sizeof(reply), "HELLO FROM SERVER 1 %s\n", ts);
        if (write(client_socket, reply, len) < 0) {
            perror("write");
            close(client_socket);
            return NULL;
        }
    }

    // Обрабатываем команды в цикле
    while (1) {
        // Читаем сообщение до '\n'
        size_t msg_len = 0;
        while ((n = read(client_socket, buffer + msg_len,
                         sizeof(buffer) - msg_len - 1)) > 0) {
            msg_len += n;
            if (buffer[msg_len - 1] == '\n' || msg_len >= sizeof(buffer) - 1)
                break;
        }
        if (n <= 0) {
            perror("read");
            break;
        }
        buffer[msg_len] = '\0';

        printf("REQUEST: %s\n", buffer);

        if (strncmp(buffer, "GET_RESOLUTION", 14) == 0) {
            char ts[64];
            int width, height;
            get_timestamp(ts, sizeof(ts));
            get_screen_resolution(&width, &height);

            int len =
                snprintf(reply, sizeof(reply), "%dx%d %s\n", width, height, ts);
            write(client_socket, reply, len);

            // Пример использования GET_PIXEL
        } else if (strncmp(buffer, "GET_PIXEL", 9) == 0) {
            char ts[64];
            int r, g, b;
            get_timestamp(ts, sizeof(ts));

            // Извлекаем координаты x и y из запроса
            int x, y;
            if (sscanf(buffer, "GET_PIXEL %d %d", &x, &y) != 2) {
                fprintf(stderr, "Invalid GET_PIXEL format\n");
                int len = snprintf(reply, sizeof(reply),
                                   "Invalid GET_PIXEL format %s\n", ts);
                write(client_socket, reply, len);
            } else {
                if (get_pixel_color(x, y, &r, &g, &b) == 0) {
                    int len = snprintf(reply, sizeof(reply), "%d %d %d %s\n", r,
                                       g, b, ts);
                    write(client_socket, reply, len);
                } else {
                    int len = snprintf(reply, sizeof(reply),
                                       "Error getting pixel color %s\n", ts);
                    write(client_socket, reply, len);
                }
            }

            // DISCONNECT — отсылаем OK и выходим
        } else if (strncmp(buffer, "DISCONNECT", 10) == 0) {
            char ts[64];
            get_timestamp(ts, sizeof(ts));
            int len = snprintf(reply, sizeof(reply),
                               "GOOD BYE FROM SERVER 1 %s\n", ts);
            write(client_socket, reply, len);
            break;

        } else {
            write(client_socket, "ERROR Unknown command\n", 22);
        }
    }

    printf("Closing connection with %s:%d\n", client_ip, client_port);
    close(client_socket);
    return NULL;
}

static void get_timestamp(char *out, size_t len) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, len, "%Y-%m-%d %H:%M:%S", &tm);
}

void get_screen_resolution(int *width, int *height) {
    Screen *screen = DefaultScreenOfDisplay(g_display);
    *width = WidthOfScreen(screen);
    *height = HeightOfScreen(screen);
}
int get_pixel_color(int x, int y, int *r, int *g, int *b) {
    // Синхронизируемся с X-сервером, чтобы гарантировать актуальность данных
    XSync(g_display, False);

    // Получаем размеры экрана
    int sw = WidthOfScreen(DefaultScreenOfDisplay(g_display));
    int sh = HeightOfScreen(DefaultScreenOfDisplay(g_display));

    // Проверяем, что координаты внутри экрана
    if (x < 0 || x >= sw || y < 0 || y >= sh) {
        fprintf(stderr, "Coordinates out of bounds (%d,%d) vs %dx%d\n", x, y,
                sw, sh);
        return -1;
    }

    // Захватываем 1×1 пиксель корневого окна
    XImage *img =
        XGetImage(g_display, g_rootwin, x, y, 1, 1, AllPlanes, ZPixmap);
    if (!img) {
        fprintf(stderr, "XGetImage failed: BadMatch?\n");
        return -1;
    }

    // Получаем значение пикселя
    unsigned long pixel = XGetPixel(img, 0, 0);
    XDestroyImage(img);

    // Переводим в RGB через колормэп
    XColor xc;
    Colormap cmap = DefaultColormap(g_display, DefaultScreen(g_display));
    xc.pixel = pixel;
    XQueryColor(g_display, cmap, &xc);

    *r = xc.red >> 8;
    *g = xc.green >> 8;
    *b = xc.blue >> 8;
    return 0;
}