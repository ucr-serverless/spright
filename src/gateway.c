/*
# Copyright 2022 University of California, Riverside
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
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_mempool.h>
#include <rte_memzone.h>

#include "http.h"
#include "io.h"
#include "spright.h"
#include "utility.h"
#include "timer.h"

#define BACKLOG (1U << 16)

#define HTTP_RESPONSE "HTTP/1.1 200 OK\r\n" \
                      "Connection: close\r\n" \
                      "Content-Type: text/plain\r\n" \
                      "Content-Length: 13\r\n" \
                      "\r\n" \
                      "Hello World\r\n"

struct server_vars {
    int sockfd;
    int epfd;
};

int peer_node_sockfds[ROUTING_TABLE_SIZE];

static void configure_keepalive(int sockfd) {
    int optval;
    socklen_t optlen = sizeof(optval);

    // Enable TCP keep-alive
    optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
        log_fatal("setsockopt(SO_KEEPALIVE)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Set TCP keep-alive parameters
    optval = 60; // Seconds before sending keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0) {
        log_fatal("setsockopt(TCP_KEEPIDLE)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    optval = 10; // Interval in seconds between keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0) {
        log_fatal("setsockopt(TCP_KEEPINTVL)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    optval = 5; // Number of unacknowledged probes before considering the connection dead
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, optlen) < 0) {
        log_fatal("setsockopt(TCP_KEEPCNT)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
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
static int get_client_info(int client_socket, char* ip_addr, int ip_addr_len) {

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
    if (getpeername(client_socket, (struct sockaddr*)&addr, &addr_len) == -1) {
        log_error("getpeername failed.");
        close(client_socket);
        return -1;
    }
    
    // Convert IP address to human-readable form
    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str)) == NULL) {
        log_error("inet_ntop failed.");
        close(client_socket);
        return -1;
    }

    client_port = ntohs(addr.sin_port);

    if (ip_addr) {
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

static int rpc_server_setup(int epfd) {
    struct sockaddr_in addr;
    int sockfd_l;
    int sockfd_c = 0;
    int optval;
    int ret;
    struct epoll_event event;

    sockfd_l = socket(AF_INET, SOCK_STREAM, 0);
    if (unlikely(sockfd_l == -1)) {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }

    optval = 1;
    ret = setsockopt(sockfd_l, SOL_SOCKET, SO_REUSEADDR, &optval,
                     sizeof(int));
    if (unlikely(ret == -1)) {
        log_error("setsockopt() error: %s", strerror(errno));
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(INTERNAL_SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(cfg->nodes[cfg->local_node_idx].ip_address);

    ret = bind(sockfd_l, (struct sockaddr *)&addr,
               sizeof(struct sockaddr_in));
    if (unlikely(ret == -1)) {
        log_error("bind() error: %s", strerror(errno));
        return -1;
    }

    /* TODO: Correct backlog? */
    ret = listen(sockfd_l, 10);
    if (unlikely(ret == -1)) {
        log_error("listen() error: %s", strerror(errno));
        return -1;
    }

    if (cfg->n_nodes == 1) {
        log_warn("No PEER NODE CONFIGURED. Terminating the RPC server...");
        goto error;
    }

    while (1) {
        sockfd_c = accept(sockfd_l, NULL, NULL);
        if (unlikely(sockfd_c == -1)) {
            log_error("accept() error: %s",
                    strerror(errno));
            goto error;
        }

        get_client_info(sockfd_c, NULL, 0);
        configure_keepalive(sockfd_c);
        event.events = EPOLLIN;
        event.data.fd = sockfd_c;

        ret = epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd_c, &event);
        if (unlikely(ret == -1)) {
            log_error("epoll_ctl() error: %s", strerror(errno));
            goto error;
        }
    }
error:
    ret = close(sockfd_l);
    // TODO: close the epoll fd gracefully
    if (unlikely(ret == -1)) {
        log_error("close() error: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int rpc_server_receive(int epfd) {
    int ret;
    int n_events;
    int i;
    int sockfd_c;
    struct epoll_event event[N_EVENTS_MAX];
    struct http_transaction *txn = NULL;

    while(1) {
        n_events = epoll_wait(epfd, event, N_EVENTS_MAX, -1);
        if (unlikely(n_events == -1)) {
            log_error("epoll_wait() error: %s",
                    strerror(errno));
            return -1;
        }

        for (i = 0; i < n_events; i++) {
            ret = rte_mempool_get(cfg->mempool, (void **)&txn);
            if (unlikely(ret < 0)) {
                log_error("rte_mempool_get() error: %s",
                        rte_strerror(-ret));
                goto error_0;
            }
            sockfd_c = event[i].data.fd;

            get_client_info(sockfd_c, NULL, 0);

            log_debug("Receiving from PEER GW.");
            ssize_t total_bytes_received = read_full(sockfd_c, txn, sizeof(*txn));
            if (total_bytes_received == -1) {
                log_error("read_full() error");
                goto error_1;
            } else if (total_bytes_received != sizeof(*txn)) {
                log_error("Incomplete transaction received: expected %ld, got %zd", sizeof(*txn), total_bytes_received);
                goto error_1;
            }

            log_debug("Bytes received: %zd. \t sizeof(*txn): %ld.", total_bytes_received, sizeof(*txn));

            // Send txn to local function
            log_debug("\tRoute id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u", 
                        txn->route_id, txn->hop_count,
                        cfg->route[txn->route_id].hop[txn->hop_count],
                        txn->next_fn);
            ret = io_tx(txn, txn->next_fn);
            if (unlikely(ret == -1)) {
                log_error("io_tx() error");
                goto error_1;
            }
        }
    }

error_1:
    rte_mempool_put(cfg->mempool, txn);
    ret = epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd_c, NULL);
    if (unlikely(ret == -1)) {
        log_error("rpc_server delete client fd error");
    }
    close(sockfd_c);
error_0:
    return -1;
}

void* rpc_server_setup_thread(void* arg) {
    int ret = rpc_server_setup(*(int*)arg);
    if (unlikely(ret == -1)) {
        log_error("rpc_server() error");
    }
    return NULL;
}

void* rpc_server_receive_thread(void* arg) {
    int ret = rpc_server_receive(*(int*)arg);
    if (unlikely(ret == -1)) {
        log_error("rpc_server() error");
    }
    return NULL;
}

static int rpc_client_setup(char *server_ip, uint16_t server_port, uint8_t peer_node_idx) {
    log_info("RPC client connects with node %u (%s:%u).", peer_node_idx,
            cfg->nodes[peer_node_idx].ip_address, INTERNAL_SERVER_PORT);

    struct sockaddr_in server_addr;
    int sockfd;
    int ret;
    int opt = 1;

    log_debug("Destination GW Server (%s:%u).", server_ip, server_port);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (unlikely(sockfd == -1)) {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }

    // Set SO_REUSEADDR to reuse the address
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(sockfd);
        return -1;
    }

    configure_keepalive(sockfd);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    ret = connect(sockfd, (struct sockaddr *)&server_addr,
                  sizeof(struct sockaddr_in));
    if (unlikely(ret == -1)) {
        log_error("connect() error: %s", strerror(errno));
        return -1;
    }

    return sockfd;
}

static int rpc_client_send(int peer_node_idx, struct http_transaction *txn) {
    log_debug("Route id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u, \
        Caller Fn: %s (#%u), RPC Handler: %s()",
        txn->route_id, txn->hop_count,
        cfg->route[txn->route_id].hop[txn->hop_count],
        txn->next_fn, txn->caller_nf, txn->caller_fn, txn->rpc_handler);

    ssize_t bytes_sent;
    int sockfd = peer_node_sockfds[peer_node_idx];

    bytes_sent = send(sockfd, txn, sizeof(*txn), 0);

    log_debug("peer_node_idx: %d \t bytes_sent: %ld \t sizeof(*txn): %ld", peer_node_idx, bytes_sent, sizeof(*txn));
    if (unlikely(bytes_sent == -1)) {
        log_error("send() error: %s", strerror(errno));
        return -1;
    }

    log_debug("rpc_client_send is done.");

    return 0;
}

// static int rpc_client_close(int peer_node_idx) {

// 	int sockfd = peer_node_sockfds[peer_node_idx];

// 	ret = close(sockfd);
// 	if (unlikely(ret == -1)) {
// 		log_error("close() error: %s", strerror(errno));
// 		return -1;
// 	}

// peer_node_sockfds[peer_node_idx] = 0;

// 	return 0;
// }

void* rpc_client_thread(void* arg) {
    int epoll_fd;
    struct epoll_event ev, events[N_EVENTS_MAX];
    int nfds;
    int ret;

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ret = add_pipes_to_epoll(epoll_fd, &ev);
    if (ret == -1) {
        return NULL;
    }

    int gcd_weight = get_gcd_weight();
    int max_weight = get_max_weight();
    int current_index = -1;
    int current_weight = max_weight;
    struct http_transaction *txn = NULL;

    while (1) {
        nfds = epoll_wait(epoll_fd, events, N_EVENTS_MAX, -1);
        if (nfds == -1) {
            log_error("epoll_wait() error: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; n++) {
            tenant_pipe* tp = (tenant_pipe*) events[n].data.ptr;

            log_debug("Tenant-%d's pipe is ready to be consumed ...", tp->tenant_id);

            while (1) {
                current_index = (current_index + 1) % cfg->n_tenants;
                if (current_index == 0) {
                    current_weight -= gcd_weight;
                    if (current_weight <= 0) {
                        current_weight = max_weight;
                    }
                }

                log_debug("Tenant ID: %d \t Assigned Weight: %d \t Current Weight: %d ",
                    current_index, tenant_pipes[current_index].weight, current_weight);

                if (current_index == tp->tenant_id &&
                        tenant_pipes[current_index].weight >= current_weight) {

                    txn = read_pipe(tp);
                    if (txn == NULL) {
                        close(tp->fd[0]);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, tp->fd[0], NULL);
                    }

                    uint8_t peer_node_idx = get_node(txn->next_fn);

                    if (peer_node_sockfds[peer_node_idx] == 0) {
                        peer_node_sockfds[peer_node_idx] = rpc_client_setup(
                                   cfg->nodes[peer_node_idx].ip_address,
                                   INTERNAL_SERVER_PORT, peer_node_idx);
                    } else if (peer_node_sockfds[peer_node_idx] < 0) {
                        log_error("Invalid socket error.");
                        return NULL;
                    }

                    ret = rpc_client_send(peer_node_idx, txn);

                    rte_mempool_put(cfg->mempool, txn);

                    break;
                }
            }
        }
    }

    close(epoll_fd);
    return NULL;
}

static int conn_accept(struct server_vars *sv)
{
    struct epoll_event event;
    int sockfd;
    int ret;

    sockfd = accept(sv->sockfd, NULL, NULL);
    if (unlikely(sockfd == -1)) {
        log_error("accept() error: %s", strerror(errno));
        goto error_0;
    }

    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.fd = sockfd;

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_ADD, sockfd, &event);
    if (unlikely(ret == -1)) {
        log_error("epoll_ctl() error: %s", strerror(errno));
        goto error_1;
    }

    return 0;

error_1:
    close(sockfd);
error_0:
    return -1;
}

static int conn_close(struct server_vars *sv, int sockfd)
{
    int ret;

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_DEL, sockfd, NULL);
    if (unlikely(ret == -1)) {
        log_error("epoll_ctl() error: %s", strerror(errno));
        goto error_1;
    }

    ret = close(sockfd);
    if (unlikely(ret == -1)) {
        log_error("close() error: %s", strerror(errno));
        goto error_0;
    }

    return 0;

error_1:
    close(sockfd);
error_0:
    return -1;
}

static int conn_read(int sockfd)
{
    struct http_transaction *txn = NULL;
    char *string = NULL;
    int ret;

    ret = rte_mempool_get(cfg->mempool, (void **)&txn);
    if (unlikely(ret < 0)) {
        log_error("rte_mempool_get() error: %s",
                rte_strerror(-ret));
        goto error_0;
    }

    get_client_info(sockfd, NULL, 0);

    log_debug("Receiving from External User.");
    txn->length_request = read(sockfd, txn->request, HTTP_MSG_LENGTH_MAX);
    if (unlikely(txn->length_request == -1)) {
        log_error("read() error: %s", strerror(errno));
        goto error_1;
    }

    txn->sockfd = sockfd;

    // TODO: parse tenant ID from HTTP request,
    // use "0" as the default tenant ID for now.
    txn->tenant_id = 0;

    string = strstr(txn->request, "/");
    if (unlikely(string == NULL)) {
        txn->route_id = 0;
    } else {
        errno = 0;
        txn->route_id = strtol(string + 1, NULL, 10);
        if (unlikely(errno != 0 || txn->route_id < 0)) {
            txn->route_id = 0;
        }
    }

    txn->hop_count = 0;

    ret = io_tx(txn, cfg->route[txn->route_id].hop[0]);
    if (unlikely(ret == -1)) {
        log_error("io_tx() error");
        goto error_1;
    }

    return 0;

error_1:
    rte_mempool_put(cfg->mempool, txn);
error_0:
    return -1;
}

static int conn_write(int *sockfd)
{
    struct http_transaction *txn = NULL;
    ssize_t bytes_sent;
    int ret;

    log_debug("Waiting for the next write.");

    ret = io_rx((void **)&txn);
    if (unlikely(ret == -1)) {
        log_error("io_rx() error");
        goto error_0;
    }

    // Inter-node Communication
    if (cfg->route[txn->route_id].hop[txn->hop_count] != fn_id) {
        log_debug("Enqueuing Tenant-%d's descriptor to weighted round robin queues.",
                    txn->tenant_id);
        ret = write_pipe(txn);
        if (unlikely(ret == -1)) {
            goto error_1;
        }

        return 1;
    }

    txn->hop_count++;
    log_debug("Next hop is %u", cfg->route[txn->route_id].hop[txn->hop_count]);
    txn->next_fn = cfg->route[txn->route_id].hop[txn->hop_count];

    // Intra-node Communication
    if (txn->hop_count < cfg->route[txn->route_id].length) {
        ret = io_tx(txn, txn->next_fn);
        if (unlikely(ret == -1)) {
            log_error("io_tx() error");
            goto error_1;
        }

        return 1;
    }

    // Respond External Client
    *sockfd = txn->sockfd;

    txn->length_response = strlen(HTTP_RESPONSE);
    memcpy(txn->response, HTTP_RESPONSE, txn->length_response);

    /* TODO: Handle incomplete writes */
    bytes_sent = write(*sockfd, txn->response, txn->length_response);
    if (unlikely(bytes_sent == -1)) {
        log_error("write() error: %s", strerror(errno));
        goto error_1;
    }

    rte_mempool_put(cfg->mempool, txn);

    return 0;

error_1:
    rte_mempool_put(cfg->mempool, txn);
error_0:
    return -1;
}

static int event_process(struct epoll_event *event, struct server_vars *sv)
{
    int ret;

    log_debug("Processing an new event.", __func__);

    if (event->data.fd == sv->sockfd) {
        log_debug("New Connection Accept.", __func__);
        ret = conn_accept(sv);
        if (unlikely(ret == -1)) {
            log_error("conn_accept() error");
            return -1;
        }
    } else if (event->events & EPOLLIN) {
        log_debug("Reading New Data.", __func__);
        ret = conn_read(event->data.fd);
        if (unlikely(ret == -1)) {
            log_error("conn_read() error");
            return -1;
        }

        if (ret == 1) {
            event->events |= EPOLLONESHOT;

            ret = epoll_ctl(sv->epfd, EPOLL_CTL_MOD, event->data.fd,
                            event);
            if (unlikely(ret == -1)) {
                log_error("epoll_ctl() error: %s",
                        strerror(errno));
                return -1;
            }
        }
    } else if (event->events & (EPOLLERR | EPOLLHUP)) {
        /* TODO: Handle (EPOLLERR | EPOLLHUP) */
        log_error("(EPOLLERR | EPOLLHUP)");

        log_debug("Error - Close the connection.", __func__);
        ret = conn_close(sv, event->data.fd);
        if (unlikely(ret == -1)) {
            log_error("conn_close() error");
            return -1;
        }
    }

    return 0;
}


/* TODO: Cleanup on errors */
static int server_init(struct server_vars *sv)
{
    struct sockaddr_in server_addr;
    struct epoll_event event;
    int optval;
    int ret;
    int rpc_svr_epfd;
    pthread_t rpc_svr_setup_thread;
    pthread_t rpc_svr_recv_thread;
    pthread_t rpc_clt_thread;

    log_info("Initializing intra-node I/O...");
    ret = io_init();
    if (unlikely(ret == -1)) {
        log_error("io_init() error");
        return -1;
    }

    ret = init_tenant_pipes();
    if (unlikely(ret == -1)) {
        return -1;
    }

    rpc_svr_epfd = epoll_create1(0);
    if (unlikely(rpc_svr_epfd == -1)) {
        log_error("epoll_create1() error: %s", strerror(errno));
        return -1;
    }

    ret = pthread_create(&rpc_svr_setup_thread, NULL, &rpc_server_setup_thread, &rpc_svr_epfd);
    if (unlikely(ret != 0)) {
        log_error("pthread_create() error: %s", strerror(ret));
        return -1;
    }

    ret = pthread_create(&rpc_svr_recv_thread, NULL, &rpc_server_receive_thread, &rpc_svr_epfd);
    if (unlikely(ret != 0)) {
        log_error("pthread_create() error: %s", strerror(ret));
        return -1;
    }

    ret = pthread_create(&rpc_clt_thread, NULL, &rpc_client_thread, NULL);
    if (unlikely(ret != 0)) {
        log_error("pthread_create() error: %s", strerror(ret));
        return -1;
    }

    log_info("Initializing server socket...");
    sv->sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (unlikely(sv->sockfd == -1)) {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }

    optval = 1;
    ret = setsockopt(sv->sockfd, SOL_SOCKET, SO_REUSEADDR, &optval,
                     sizeof(int));
    if (unlikely(ret == -1)) {
        log_error("setsockopt() error: %s", strerror(errno));
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(EXTERNAL_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(sv->sockfd, (struct sockaddr *)&server_addr,
               sizeof(struct sockaddr_in));
    if (unlikely(ret == -1)) {
        log_error("bind() error: %s", strerror(errno));
        return -1;
    }

    ret = listen(sv->sockfd, BACKLOG);
    if (unlikely(ret == -1)) {
        log_error("listen() error: %s", strerror(errno));
        return -1;
    }

    log_info("Initializing epoll...");
    sv->epfd = epoll_create1(0);
    if (unlikely(sv->epfd == -1)) {
        log_error("epoll_create1() error: %s", strerror(errno));
        return -1;
    }

    event.events = EPOLLIN;
    event.data.fd = sv->sockfd;

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_ADD, sv->sockfd, &event);
    if (unlikely(ret == -1)) {
        log_error("epoll_ctl() error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/* TODO: Cleanup on errors */
static int server_exit(struct server_vars *sv)
{
    int ret;

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_DEL, sv->sockfd, NULL);
    if (unlikely(ret == -1)) {
        log_error("epoll_ctl() error: %s", strerror(errno));
        return -1;
    }

    ret = close(sv->epfd);
    if (unlikely(ret == -1)) {
        log_error("close() error: %s", strerror(errno));
        return -1;
    }

    ret = close(sv->sockfd);
    if (unlikely(ret == -1)) {
        log_error("close() error: %s", strerror(errno));
        return -1;
    }

    ret = io_exit();
    if (unlikely(ret == -1)) {
        log_error("io_exit() error");
        return -1;
    }

    return 0;
}

static int server_process_rx(void *arg)
{
    struct epoll_event event[N_EVENTS_MAX];
    struct server_vars *sv = NULL;
    int n_fds;
    int ret;
    int i;

    sv = arg;

    while (1) {
        n_fds = epoll_wait(sv->epfd, event, N_EVENTS_MAX, -1);
        if (unlikely(n_fds == -1)) {
            log_error("epoll_wait() error: %s",
                    strerror(errno));
            return -1;
        }

        log_debug("%d NEW EVENTS READY =======", n_fds);

        for (i = 0; i < n_fds; i++) {
            ret = event_process(&event[i], sv);
            if (unlikely(ret == -1)) {
                log_error("event_process() error");
                return -1;
            }
        }
    }

    return 0;
}

static int server_process_tx(void *arg)
{
    struct server_vars *sv = NULL;
    int sockfd;
    int ret;

    sv = arg;

    while (1) {
        ret = conn_write(&sockfd);
        if (unlikely(ret == -1)) {
            log_error("conn_write() error");
            return -1;
        } else if (ret == 1) {
            continue;
        }

        log_debug("Closing the connection after TX.\n");
        ret = conn_close(sv, sockfd);
        if (unlikely(ret == -1)) {
            log_error("conn_close() error");
            return -1;
        }
    }

    return 0;
}

static void metrics_collect(void)
{
    while (1) {
        sleep(30);
    }
}

static int gateway(void)
{
    const struct rte_memzone *memzone = NULL;
    unsigned int lcore_worker[2];
    struct server_vars sv;
    int ret;
    memset(peer_node_sockfds, 0, sizeof(peer_node_sockfds));

    fn_id = 0;

    memzone = rte_memzone_lookup(MEMZONE_NAME);
    if (unlikely(memzone == NULL)) {
        log_error("rte_memzone_lookup() error");
        goto error_0;
    }

    cfg = memzone->addr;

    ret = server_init(&sv);
    if (unlikely(ret == -1)) {
        log_error("server_init() error");
        goto error_0;
    }

    lcore_worker[0] = rte_get_next_lcore(rte_get_main_lcore(), 1, 1);
    if (unlikely(lcore_worker[0] == RTE_MAX_LCORE)) {
        log_error("rte_get_next_lcore() error");
        goto error_1;
    }

    lcore_worker[1] = rte_get_next_lcore(lcore_worker[0], 1, 1);
    if (unlikely(lcore_worker[1] == RTE_MAX_LCORE)) {
        log_error("rte_get_next_lcore() error");
        goto error_1;
    }

    ret = rte_eal_remote_launch(server_process_rx, &sv, lcore_worker[0]);
    if (unlikely(ret < 0)) {
        log_error("rte_eal_remote_launch() error: %s",
                rte_strerror(-ret));
        goto error_1;
    }

    ret = rte_eal_remote_launch(server_process_tx, &sv, lcore_worker[1]);
    if (unlikely(ret < 0)) {
        log_error("rte_eal_remote_launch() error: %s",
                rte_strerror(-ret));
        goto error_1;
    }

    metrics_collect();

    ret = rte_eal_wait_lcore(lcore_worker[0]);
    if (unlikely(ret == -1)) {
        log_error("server_process_rx() error");
        goto error_1;
    }

    ret = rte_eal_wait_lcore(lcore_worker[1]);
    if (unlikely(ret == -1)) {
        log_error("server_process_tx() error");
        goto error_1;
    }

    ret = server_exit(&sv);
    if (unlikely(ret == -1)) {
        log_error("server_exit() error");
        goto error_0;
    }

    return 0;

error_1:
    server_exit(&sv);
error_0:
    return -1;
}

int main(int argc, char **argv)
{
    log_set_level_from_env();

    int ret;

    ret = rte_eal_init(argc, argv);
    if (unlikely(ret == -1)) {
        log_error("rte_eal_init() error: %s",
                rte_strerror(rte_errno));
        goto error_0;
    }

    ret = gateway();
    if (unlikely(ret == -1)) {
        log_error("gateway() error");
        goto error_1;
    }

    ret = rte_eal_cleanup();
    if (unlikely(ret < 0)) {
        log_error("rte_eal_cleanup() error: %s",
                rte_strerror(-ret));
        goto error_0;
    }

    return 0;

error_1:
    rte_eal_cleanup();
error_0:
    return 1;
}
