#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    // Windows-specific headers and settings
    #include <winsock2.h>
    #include <ws2tcpip.h>  // Include this for addrinfo on Windows
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    typedef int socklen_t;
#else
    // Linux-specific headers
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <errno.h>
    #include <arpa/inet.h>
#endif

#define S1_PORT 8001
#define S2_PORT 8002
#define BS 4096

#ifdef _WIN32
// Windows-specific initialization and cleanup
int init_winsock() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

void cleanup_winsock() {
    WSACleanup();
}
#endif

// Function to read a greeting from the socket
void read_greeting(int sd) {
    char buf[BS];
    size_t total = 0;
    ssize_t n;
    while ((n = recv(sd, buf + total, sizeof(buf) - total - 1, 0)) > 0) {
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

// Function to connect to a server
int conn(const char *h, int p) {
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    int sd = -1;

    snprintf(port_str, sizeof(port_str), "%d", p);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // Only IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP

    int err = getaddrinfo(h, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sd == -1)
            continue;

        if (connect(sd, rp->ai_addr, rp->ai_addrlen) != -1)
            break; // Successful connection

        close(sd);
        sd = -1;
    }

    freeaddrinfo(res);

    if (sd == -1) {
        perror("connect");
    }

    return sd;
}

// Function to close a socket
void cl(int *s) {
    if (*s != -1) {
        close(*s);
        *s = -1;
    }
}

// Function to send a command to the server and print the response
void cmd(int sd, const char *c) {
    if (sd < 0) {
        puts("Not connected");
        return;
    }

    char buf[BS];
    int cmd_len = snprintf(buf, sizeof(buf), "%s\n", c);
    if (send(sd, buf, cmd_len, 0) != cmd_len) {
        perror("write");
        return;
    }

    // Read the response until the first '\n'
    size_t total = 0;
    ssize_t n;
    while ((n = recv(sd, buf + total, sizeof(buf) - total - 1, 0)) > 0) {
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

// Function to display the menu
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
#ifdef _WIN32
    // Initialize WinSock for Windows
    if (init_winsock() != 0) {
        printf("WSAStartup failed\n");
        return -1;
    }
#endif

    const char *host = (argc > 1 ? argv[1] : "127.0.0.1");

    printf("host=%s\n", host);
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
            // Clear stdin after scanf
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

#ifdef _WIN32
    // Cleanup WinSock for Windows
    cleanup_winsock();
#endif

    return 0;
}
