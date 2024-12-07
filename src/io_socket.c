#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "io.h"
#include "log.h"
#include "spright.h"

// TODO: Find a way to obtain from configuration file
#define GATEWAY_IP "127.0.0.1"
#define BASE_PORT 8000

#define MAX_COMPONENTS 5

struct metadata
{
    int fn_id;
    void *obj;
};

static int socket_epoll_fd;
static int sockfd_tx[MAX_COMPONENTS];

void* listener_func(void* arg) {
    struct epoll_event ev;

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_sock = accept(sockfd_tx[fn_id], (struct sockaddr*)&client_addr, &addr_len);

        if (new_sock == -1) {
            log_error("accept() error: %s", strerror(errno));
            continue; // Ignore failed accept and retry
        }

        // Register the new connection to epoll
        ev.events = EPOLLIN;
        ev.data.fd = new_sock;

        if (epoll_ctl(socket_epoll_fd, EPOLL_CTL_ADD, new_sock, &ev) == -1) {
            log_error("epoll_ctl() error: %s", strerror(errno));
            close(new_sock);
            continue;
        }
    }

    return NULL;
}

int io_init(void) {
    struct sockaddr_in serv_addr;
    pthread_t listener_thread;
    int optval = 1;

    // Initialize epoll instance
    socket_epoll_fd = epoll_create1(0);
    if (socket_epoll_fd == -1) {
        log_error("epoll_create1() error: %s", strerror(errno));
        return -1;
    }

    // Create server socket (listening socket)
    sockfd_tx[fn_id] = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_tx[fn_id] == -1) {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }

    // Set socket options
    if (setsockopt(sockfd_tx[fn_id], SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
        log_error("setsockopt() error: %s", strerror(errno));
        close(sockfd_tx[fn_id]);
        return -1;
    }

    // Bind the socket to the appropriate port
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(BASE_PORT + fn_id);
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(sockfd_tx[fn_id], (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        log_error("bind() error: %s", strerror(errno));
        close(sockfd_tx[fn_id]);
        return -1;
    }

    if (listen(sockfd_tx[fn_id], 10) == -1) {
        log_error("listen() error: %s", strerror(errno));
        close(sockfd_tx[fn_id]);
        return -1;
    }

    log_info("Socket %d listening on port %d", fn_id, BASE_PORT + fn_id);

    // Start the listener thread
    pthread_create(&listener_thread, NULL, listener_func, NULL);
    pthread_detach(listener_thread);

    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (i == fn_id) continue;

        struct sockaddr_in target_addr;

        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(BASE_PORT + i);
        target_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        int client_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (client_sock == -1) {
            log_error("socket() error: %s", strerror(errno));
            continue;
        }

        // Attempt to connect with retries because socket servers may not be up yet
        if (retry_connect(client_sock, (struct sockaddr*)&target_addr) == -1) {
            log_error("retry_connect() failed for component %d", i);
            close(client_sock);
            continue;
        }

        sockfd_tx[i] = client_sock;
    }

    return 0;
}

int io_exit(void) {
    if (fn_id == -1)
    {
        return 0;
    }

    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (sockfd_tx[i] > 0) {
            close(sockfd_tx[i]);
        }
    }

    return 0;
}

int io_rx(void** obj) {
    struct epoll_event events[MAX_COMPONENTS];
    struct metadata m;
    int error = 0;
    socklen_t len = sizeof(error);
    int n_fds;

    // Wait for incoming data on any socket
    n_fds = epoll_wait(socket_epoll_fd, events, MAX_COMPONENTS, -1);
    if (n_fds == -1) {
        log_error("epoll_wait() error: %s", strerror(errno));
        return -1;
    }

    for (int i = 0; i < n_fds; i++) {
        int fd = events[i].data.fd;

        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
            log_error("getsockopt() error: %s", strerror(errno));
            return -1;
        }

        if (error != 0) {
            log_error("Socket error before recv(): %s", strerror(error));
            return -1;
        }

        log_debug("Socket %d receiving data on fd %d", fn_id, fd);
        if (unlikely(retry_recv(fd, &m, sizeof(struct metadata), 0) == -1)) {
            log_error("recv() error: %s", strerror(errno));
            return -1;
        }

        *obj = m.obj;

        return 0;
    }

    return 0;
}

int io_tx(void* obj, uint8_t next_fn) {
    log_debug("Socket %d sending to %d using file descriptor %d", fn_id, next_fn, sockfd_tx[(int)next_fn]);

    struct metadata m;

    m.fn_id = next_fn;
    m.obj = obj;

    if (unlikely(retry_send(sockfd_tx[next_fn], &m, sizeof(struct metadata), 0) == -1))
    {
        log_error("send() error: %s", strerror(errno));
        return -1;
    }

    return 0;
}
