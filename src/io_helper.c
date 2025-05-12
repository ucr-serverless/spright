/*
# Copyright 2025 University of California, Riverside
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <rte_branch_prediction.h>
#include <rte_errno.h>
#include <rte_mempool.h>

#include "io.h"

#define BACKLOG (1U << 16)
#define MAX_RETRIES 5
#define RETRY_DELAY_US 5000 // 5 milliseconds

tenant_pipe tenant_pipes[MAX_TENANTS];

/*
 * Calculate the Greatest Common Divisor
 */
static int gcd(int a, int b)
{
    while (b != 0)
    {
        int temp = b;
        b = a % b;
        a = temp;
    }

    return a;
}

/*
 * Calculate the GCD of a list of numbers
 */
int get_gcd_weight(void)
{
    if (cfg->n_tenants == 0)
        return 0;

    int result = cfg->tenants[0].weight;

    for (int i = 1; i < cfg->n_tenants; i++)
    {
        result = gcd(result, cfg->tenants[i].weight);
    }

    log_info("GCD weight: %d", result);

    return result;
}

int get_max_weight(void)
{
    int max_val = cfg->tenants[0].weight;

    for (int i = 1; i < cfg->n_tenants; i++)
    {
        if (cfg->tenants[i].weight > max_val)
        {
            max_val = cfg->tenants[i].weight;
        }
    }

    return max_val;
}

/*
 * Set file descriptor as non-blocking
 */
int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        log_error("fcntl(F_GETFL) error: %s", strerror(errno));
        return -1;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1)
    {
        log_error("fcntl(F_SETFL) error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int write_pipe(struct http_transaction *txn)
{
    uint32_t tenant_id = txn->tenant_id;

    ssize_t bytes_written = write(tenant_pipes[tenant_id].fd[1], &txn, sizeof(struct http_transaction *));
    if (unlikely(bytes_written == -1))
    {
        log_error("write() error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

struct http_transaction *read_pipe(tenant_pipe *tp)
{
    struct http_transaction *txn = NULL;

    int bytes_read = read(tp->fd[0], &txn, sizeof(struct http_transaction *));
    if (bytes_read > 0)
    {
        return txn;
    }
    else if (bytes_read == -1 && errno != EAGAIN)
    {
        log_error("Error while reading pipe: %s", strerror(errno));
    }
    else if (bytes_read == 0)
    {
        log_error("Unexpected end of pipe: %s", strerror(errno));
    }

    return NULL;
}

/*
 * Initialize tenant pipes and weights
 */
int init_tenant_pipes(void)
{
    int num_tenants = cfg->n_tenants;

    log_info("Initializing %d tenant pipes and weights ...", num_tenants);

    for (int i = 0; i < num_tenants; i++)
    {
        if (pipe(tenant_pipes[i].fd) == -1)
        {
            log_error("pipe() error: %s", strerror(errno));
            return -1;
        }
        tenant_pipes[i].weight = cfg->tenants[i].weight;
        tenant_pipes[i].tenant_id = (uint32_t)i;
    }

    return 0;
}

int add_regular_pipe_to_epoll(int epoll_fd, struct epoll_event *ev, int pipe_fd)
{
    set_nonblocking(pipe_fd);
    ev->events = EPOLLIN;
    ev->data.fd = pipe_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pipe_fd, ev) == -1)
    {
        log_error("epoll_ctl(EPOLL_CTL_ADD): %s", strerror(errno));
        return -1;
    }

    return 0;
}

int add_weighted_pipes_to_epoll(int epoll_fd, struct epoll_event *ev)
{
    for (int i = 0; i < cfg->n_tenants; i++)
    {
        set_nonblocking(tenant_pipes[i].fd[0]);
        ev->events = EPOLLIN;
        ev->data.ptr = &tenant_pipes[i];

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tenant_pipes[i].fd[0], ev) == -1)
        {
            log_error("epoll_ctl(EPOLL_CTL_ADD): %s", strerror(errno));
            return -1;
        }
    }

    return 0;
}

// Helper function to read exactly count bytes from fd into buf
ssize_t read_full(int fd, void *buf, size_t count)
{
    size_t bytes_read = 0;
    ssize_t result;

    while (bytes_read < count)
    {
        result = read(fd, (char *)buf + bytes_read, count - bytes_read);

        if (result < 0)
        {
            // Error occurred
            if (errno == EINTR)
            {
                // Interrupted by signal, continue reading
                continue;
            }
            log_error("read() error: %s", strerror(errno));
            return -1;
        }
        else if (result == 0)
        {
            // EOF reached
            break;
        }

        bytes_read += result;
    }

    return bytes_read;
}

/*
 * This approach will attempt to connect to the server multiple times,
 * giving it some time to become ready. If the connection is not successful
 * within the specified number of retries, the function will return an error.
 */
int retry_connect(int sockfd, struct sockaddr *addr)
{
    int attempts = 0;
    int ret;

    do
    {
        ret = connect(sockfd, addr, sizeof(struct sockaddr_in));
        if (ret == 0)
        {
            break;
        }
        else
        {
            attempts++;
            log_warn("connect() error: %s. Retrying %d times ...", strerror(errno), attempts);
            usleep(RETRY_DELAY_US);
        }
    } while (ret == -1 && attempts < MAX_RETRIES);

    return ret;
}

/**
 * @brief Input a client socket fd, output the IP adderss and port used by the client
 *
 *
 * @param[in] client_socket: The client socket fd.
 * @param[out] ip_addr: The client's IP address in human readable form,
 * e.g., "10.0.1.1". if ip_addr is NULL, it is not copied.
 * @param[out] ip_addr_len: the length of the ip_addr, at least 16 if ip_addr is not NULL
 * @return The port of the client socket.
 */
int get_client_info(int client_socket, char *ip_addr, int ip_addr_len)
{

#ifdef ENABLE_TIMER
    struct timespec t_start;
    struct timespec t_end;

    get_monotonic_time(&t_start);
#endif

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int client_port;

    log_debug("run getpeername.", __func__);

    // Get the address of the peer (client) connected to the socket
    if (getpeername(client_socket, (struct sockaddr *)&addr, &addr_len) == -1)
    {
        log_error("getpeername failed.");
        close(client_socket);
        return -1;
    }

    // Convert IP address to human-readable form
    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str)) == NULL)
    {
        log_error("inet_ntop failed.");
        close(client_socket);
        return -1;
    }

    client_port = ntohs(addr.sin_port);

    if (ip_addr)
    {
        assert(ip_addr_len >= INET_ADDRSTRLEN);
        strncpy(ip_addr, ip_str, ip_addr_len);
        log_debug("client address copied");
    }

    // Print client's IP address and port number
    log_debug("Client address: %s:%d", ip_str, client_port);

#ifdef ENABLE_TIMER
    get_monotonic_time(&t_end);
    log_debug("[%s] execution latency: %ld.", __func__, get_elapsed_time_nano(&t_start, &t_end));
#endif

    return client_port;
}

int create_server_socket(const char *ip, int port)
{
    int server_fd;
    int ret;
    int optval;
    struct sockaddr_in addr;

    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (unlikely(server_fd == -1))
    {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }

    optval = 1;
    ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    if (unlikely(ret == -1))
    {
        log_error("setsockopt() error: %s", strerror(errno));
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    ret = bind(server_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (unlikely(ret == -1))
    {
        log_error("bind() error: %s", strerror(errno));
        return -1;
    }

    ret = listen(server_fd, BACKLOG);
    if (unlikely(ret == -1))
    {
        log_error("listen() error: %s", strerror(errno));
        return -1;
    }

    return server_fd;
}

void configure_keepalive(int sockfd)
{
    int optval;
    socklen_t optlen = sizeof(optval);

    // Enable TCP keep-alive
    optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0)
    {
        log_fatal("setsockopt(SO_KEEPALIVE)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Set TCP keep-alive parameters
    optval = 60; // Seconds before sending keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0)
    {
        log_fatal("setsockopt(TCP_KEEPIDLE)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    optval = 10; // Interval in seconds between keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0)
    {
        log_fatal("setsockopt(TCP_KEEPINTVL)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    optval = 5; // Number of unacknowledged probes before considering the connection dead
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, optlen) < 0)
    {
        log_fatal("setsockopt(TCP_KEEPCNT)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
}
