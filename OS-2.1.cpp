#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define MAX_CLIENTS 5

int server_sock = -1;
int client_sockets[MAX_CLIENTS];
volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
    if (sig == SIGHUP) {
        for (int i = MAX_CLIENTS - 1; i >= 0; --i) {
            if (client_sockets[i] != -1) {
                printf("Closing connection for client %d due to SIGHUP.\n", i);
                close(client_sockets[i]);
                client_sockets[i] = -1;
                return;
            }
        }
    } else {
        running = 0;
    }
}

int main() {
    struct sockaddr_in server_addr;
    int new_socket;

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        client_sockets[i] = -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGHUP, &sa, NULL);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(3333);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, MAX_CLIENTS) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    running = 1;

    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        int max_fd = server_sock;

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (client_sockets[i] != -1) {
                FD_SET(client_sockets[i], &read_fds);
                max_fd = (client_sockets[i] > max_fd) ? client_sockets[i] : max_fd;
            }
        }

        struct timespec timeout;
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;

        sigset_t blockedMask;
        sigset_t origMask;
        sigemptyset(&blockedMask);
        sigaddset(&blockedMask, SIGHUP);

        if (pselect(max_fd + 1, &read_fds, NULL, NULL, &timeout, &blockedMask) < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("pselect");
                break;
            }
        }

        if (FD_ISSET(server_sock, &read_fds)) {
            if ((new_socket = accept(server_sock, NULL, NULL)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (client_sockets[i] == -1) {
                    printf("A new connection has been accepted\n");
                    client_sockets[i] = new_socket;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (client_sockets[i] != -1 && FD_ISSET(client_sockets[i], &read_fds)) {
                char buffer[1024];
                ssize_t bytes_received = recv(client_sockets[i], buffer, sizeof(buffer), 0);

                if (bytes_received > 0) {
                    printf("Data received from client %d: %ld bytes\n", i, bytes_received);

                    if (strncmp(buffer, "quit", 4) == 0) {
                        printf("Client %d sent 'quit'. Closing the connection.\n", i);
                        close(client_sockets[i]);
                        client_sockets[i] = -1;
                        running = 0;
                        break;
                    }
                } else if (bytes_received == 0) {
                    printf("Client %d disconnected\n", i);
                    close(client_sockets[i]);
                    client_sockets[i] = -1;
                } else {
                    perror("receive");
                }
            }
        }
    }

    close(server_sock);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_sockets[i] != -1) {
            close(client_sockets[i]);
        }
    }

    return 0;
}
