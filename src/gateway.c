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
#include "timer.h"
#include "utility.h"

#define IS_SERVER_TRUE 1
#define IS_SERVER_FALSE 0

#define HTTP_RESPONSE                                                                                                  \
    "HTTP/1.1 200 OK\r\n"                                                                                              \
    "Connection: close\r\n"                                                                                            \
    "Content-Type: text/plain\r\n"                                                                                     \
    "Content-Length: 13\r\n"                                                                                           \
    "\r\n"                                                                                                             \
    "Hello World\r\n"

struct server_vars
{
    int rpc_svr_sockfd; // Handle intra-cluster RPCs
    int ing_svr_sockfd; // Handle external clients
    int epfd;
};

typedef struct {
    int sockfd;
    int is_server;     // 1 for server_fd, 0 for client_fd
    int peer_svr_fd;   // Peer server_fd (for client_fd)
} sockfd_context_t;

int peer_node_sockfds[ROUTING_TABLE_SIZE];

static int dispatch_msg_to_fn(struct http_transaction *txn)
{
    int ret;

    if (txn->next_fn != cfg->route[txn->route_id].hop[txn->hop_count])
    {
        if (txn->hop_count == 0)
        {
            txn->next_fn = cfg->route[txn->route_id].hop[txn->hop_count];
            log_debug("Dispatcher receives a request from conn_read.");
        }
        else
        {
            log_debug("Dispatcher receives a request from conn_write or rpc_server.");
        }
    }

    ret = io_tx(txn, txn->next_fn);
    if (unlikely(ret == -1))
    {
        log_error("io_tx() error");
        return -1;
    }

    return 0;
}

static int rpc_server_receive(int sockfd)
{
    int ret;
    struct http_transaction *txn = NULL;

    ret = rte_mempool_get(cfg->mempool, (void **)&txn);
    if (unlikely(ret < 0))
    {
        log_error("rte_mempool_get() error: %s", rte_strerror(-ret));
        goto error_0;
    }

    log_debug("Receiving message from remote gateway.");
    ssize_t total_bytes_received = read_full(sockfd, txn, sizeof(*txn));
    if (total_bytes_received == -1)
    {
        log_error("read_full() error");
        goto error_1;
    }
    else if (total_bytes_received != sizeof(*txn))
    {
        log_error("Incomplete transaction received: expected %ld, got %zd", sizeof(*txn), total_bytes_received);
        goto error_1;
    }

    log_debug("Bytes received: %zd. \t sizeof(*txn): %ld.", total_bytes_received, sizeof(*txn));

    // Send txn to local function
    log_debug("Route id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u", txn->route_id, txn->hop_count,
                cfg->route[txn->route_id].hop[txn->hop_count], txn->next_fn);

    ret = dispatch_msg_to_fn(txn);
    if (unlikely(ret == -1))
    {
        log_error("dispatch_msg_to_fn() error: %s", strerror(errno));
        goto error_1;
    }

    return 0;

error_1:
    rte_mempool_put(cfg->mempool, txn);
    close(sockfd);
error_0:
    return -1;
}

static int rpc_client_setup(char *server_ip, uint16_t server_port, uint8_t peer_node_idx)
{
    log_info("RPC client connects with node %u (%s:%u).", peer_node_idx, cfg->nodes[peer_node_idx].ip_address,
             INTERNAL_SERVER_PORT);

    struct sockaddr_in server_addr;
    int sockfd;
    int ret;
    int opt = 1;

    log_debug("Destination Gateway Address (%s:%u).", server_ip, server_port);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (unlikely(sockfd == -1))
    {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }

    // Set SO_REUSEADDR to reuse the address
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(sockfd);
        return -1;
    }

    configure_keepalive(sockfd);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    ret = retry_connect(sockfd, (struct sockaddr *)&server_addr);
    if (unlikely(ret == -1))
    {
        log_error("connect() failed: %s", strerror(errno));
        return -1;
    }

    return sockfd;
}

static int rpc_client_send(int peer_node_idx, struct http_transaction *txn)
{
    log_debug("Route id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u, \
        Caller Fn: %s (#%u), RPC Handler: %s()",
              txn->route_id, txn->hop_count, cfg->route[txn->route_id].hop[txn->hop_count], txn->next_fn,
              txn->caller_nf, txn->caller_fn, txn->rpc_handler);

    ssize_t bytes_sent;
    int sockfd = peer_node_sockfds[peer_node_idx];

    bytes_sent = send(sockfd, txn, sizeof(*txn), 0);

    log_debug("sockfd: %d, peer_node_idx: %d \t bytes_sent: %ld \t sizeof(*txn): %ld", sockfd, peer_node_idx, bytes_sent, sizeof(*txn));
    if (unlikely(bytes_sent == -1))
    {
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

int rpc_client(struct http_transaction *txn)
{
    int ret;

    uint8_t peer_node_idx = get_node(txn->next_fn);

    if (peer_node_sockfds[peer_node_idx] == 0)
    {
        peer_node_sockfds[peer_node_idx] =
            rpc_client_setup(cfg->nodes[peer_node_idx].ip_address, INTERNAL_SERVER_PORT, peer_node_idx);
    }
    else if (peer_node_sockfds[peer_node_idx] < 0)
    {
        log_error("Invalid socket error.");
        return -1;
    }

    ret = rpc_client_send(peer_node_idx, txn);
    if (unlikely(ret == -1))
    {
        log_error("rpc_client_send() failed: %s", strerror(errno));
        return -1;
    }

    rte_mempool_put(cfg->mempool, txn);

    return 0;
}

static int conn_accept(int svr_sockfd, struct server_vars *sv)
{
    struct epoll_event event;
    int clt_sockfd;
    int ret;

    clt_sockfd = accept(svr_sockfd, NULL, NULL);
    if (unlikely(clt_sockfd == -1))
    {
        log_error("accept() error: %s", strerror(errno));
        goto error_0;
    }

    sockfd_context_t *clt_sk_ctx = malloc(sizeof(sockfd_context_t));
    clt_sk_ctx->sockfd      = clt_sockfd;
    clt_sk_ctx->is_server   = IS_SERVER_FALSE;
    clt_sk_ctx->peer_svr_fd = svr_sockfd;

    /* Configure RPC connection keepalive 
     * TODO: keep external connection alive 
     */
    if (svr_sockfd == sv->rpc_svr_sockfd)
    {
        log_debug("Set RPC connection to keep alive.");
        configure_keepalive(clt_sockfd);
        event.events = EPOLLIN;
    } else // svr_sockfd == sv->ing_svr_sockfd
    {
        event.events = EPOLLIN | EPOLLONESHOT;
    }

    event.data.ptr = clt_sk_ctx;

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_ADD, clt_sockfd, &event);
    if (unlikely(ret == -1))
    {
        log_error("epoll_ctl() error: %s", strerror(errno));
        goto error_1;
    }

    return 0;

error_1:
    close(clt_sockfd);
    free(clt_sk_ctx);
error_0:
    return -1;
}

static int conn_close(struct server_vars *sv, int sockfd)
{
    int ret;

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_DEL, sockfd, NULL);
    if (unlikely(ret == -1))
    {
        log_error("epoll_ctl() error: %s", strerror(errno));
        goto error_1;
    }

    ret = close(sockfd);
    if (unlikely(ret == -1))
    {
        log_error("close() error: %s", strerror(errno));
        goto error_0;
    }

    return 0;

error_1:
    close(sockfd);
error_0:
    return -1;
}

static void parse_route_id(struct http_transaction *txn)
{
    const char *string = strstr(txn->request, "/");

    if (unlikely(string == NULL)) {
        txn->route_id = 0;
    } else {
        // Skip consecutive slashes in one step
        string += strspn(string, "/");

        errno = 0;
        txn->route_id = strtol(string, NULL, 10);
        if (unlikely(errno != 0 || txn->route_id < 0)) {
            txn->route_id = 0;
        }
    }

    log_debug("Route ID: %d", txn->route_id);
}

static int conn_read(int sockfd, void* sk_ctx)
{
    struct http_transaction *txn = NULL;
    int ret;

    ret = rte_mempool_get(cfg->mempool, (void **)&txn);
    if (unlikely(ret < 0))
    {
        log_error("rte_mempool_get() error: %s", rte_strerror(-ret));
        goto error_0;
    }

    log_debug("Receiving from External User.");
    txn->length_request = read(sockfd, txn->request, HTTP_MSG_LENGTH_MAX);
    if (unlikely(txn->length_request == -1))
    {
        log_error("read() error: %s", strerror(errno));
        goto error_1;
    }

    txn->sockfd = sockfd;
    txn->sk_ctx = sk_ctx;

    // TODO: parse tenant ID from HTTP request,
    // use "0" as the default tenant ID for now.
    txn->tenant_id = 0;

    parse_route_id(txn);

    txn->hop_count = 0;

    ret = dispatch_msg_to_fn(txn);
    if (unlikely(ret == -1))
    {
        log_error("dispatch_msg_to_fn() error: %s", strerror(errno));
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

    log_debug("Waiting for the next TX event.");

    ret = io_rx((void **)&txn);
    if (unlikely(ret == -1))
    {
        log_error("io_rx() error");
        goto error_0;
    }

    log_debug("Route id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u", txn->route_id, txn->hop_count,
                cfg->route[txn->route_id].hop[txn->hop_count], txn->next_fn);

    // Inter-node Communication (use rpc_client method)
    if (cfg->route[txn->route_id].hop[txn->hop_count] != fn_id)
    {
        ret = rpc_client(txn);
        if (unlikely(ret == -1))
        {
            goto error_1;
        }

        return 1;
    }

    txn->hop_count++;
    log_debug("Next hop is Fn %u", cfg->route[txn->route_id].hop[txn->hop_count]);
    txn->next_fn = cfg->route[txn->route_id].hop[txn->hop_count];

    // Intra-node Communication (use io_tx() method)
    if (txn->hop_count < cfg->route[txn->route_id].length)
    {
        ret = dispatch_msg_to_fn(txn);
        if (unlikely(ret == -1))
        {
            log_error("dispatch_msg_to_fn() error: %s", strerror(errno));
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
    if (unlikely(bytes_sent == -1))
    {
        log_error("write() error: %s", strerror(errno));
        goto error_1;
    }

    free(txn->sk_ctx);
    rte_mempool_put(cfg->mempool, txn);

    return 0;

error_1:
    free(txn->sk_ctx);
    rte_mempool_put(cfg->mempool, txn);
error_0:
    return -1;
}

static int event_process(struct epoll_event *event, struct server_vars *sv)
{
    int ret;

    log_debug("Processing an new RX event.");

    sockfd_context_t *sk_ctx = (sockfd_context_t *)event->data.ptr;

    log_debug("sk_ctx->sockfd: %d \t sv->rpc_svr_sockfd: %d", sk_ctx->sockfd, sv->rpc_svr_sockfd);

    if (sk_ctx->is_server)
    {
        log_debug("Accepting new connection on %s.", sk_ctx->sockfd == sv->rpc_svr_sockfd ? "RPC server" : "Ingress server");
        ret = conn_accept(sk_ctx->sockfd, sv);
        if (unlikely(ret == -1))
        {
            log_error("conn_accept() error");
            return -1;
        }
    }
    else if (event->events & EPOLLIN)
    {
        if (sk_ctx->peer_svr_fd == sv->ing_svr_sockfd)
        {
            log_debug("Reading new data from external client.");
            ret = conn_read(sk_ctx->sockfd, sk_ctx);
            if (unlikely(ret == -1))
            {
                log_error("conn_read() error");
                return -1;
            }
        } else if (sk_ctx->peer_svr_fd == sv->rpc_svr_sockfd)
        {
            log_debug("Reading new data from RPC client.");
            ret = rpc_server_receive(sk_ctx->sockfd);
            if (unlikely(ret == -1))
            {
                log_error("rpc_server_receive() error");
                return -1;
            }
        } else 
        {
            log_error("Unknown peer_svr_fd");
            return -1;
        }

        if (ret == 1)
        {
            event->events |= EPOLLONESHOT;

            ret = epoll_ctl(sv->epfd, EPOLL_CTL_MOD, sk_ctx->sockfd, event);
            if (unlikely(ret == -1))
            {
                log_error("epoll_ctl() error: %s", strerror(errno));
                return -1;
            }
        }
    }
    else if (event->events & (EPOLLERR | EPOLLHUP))
    {
        /* TODO: Handle (EPOLLERR | EPOLLHUP) */
        log_error("(EPOLLERR | EPOLLHUP)");

        log_debug("Error - Close the connection.");
        ret = conn_close(sv, sk_ctx->sockfd);
        free(sk_ctx);
        if (unlikely(ret == -1))
        {
            log_error("conn_close() error");
            return -1;
        }
    }

    return 0;
}

/* TODO: Cleanup on errors */
static int server_init(struct server_vars *sv)
{
    int ret;

    log_info("Initializing intra-node I/O...");
    ret = io_init();
    if (unlikely(ret == -1))
    {
        log_error("io_init() error");
        return -1;
    }

    log_info("Initializing Ingress and RPC server sockets...");
    sv->rpc_svr_sockfd = create_server_socket(cfg->nodes[cfg->local_node_idx].ip_address, INTERNAL_SERVER_PORT);
    if (unlikely(sv->rpc_svr_sockfd == -1))
    {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }
    sockfd_context_t *rpc_svr_sk_ctx = malloc(sizeof(sockfd_context_t));
    rpc_svr_sk_ctx->sockfd = sv->rpc_svr_sockfd;
    rpc_svr_sk_ctx->is_server = IS_SERVER_TRUE;
    rpc_svr_sk_ctx->peer_svr_fd = -1;

    sv->ing_svr_sockfd = create_server_socket(cfg->nodes[cfg->local_node_idx].ip_address, EXTERNAL_SERVER_PORT);
    if (unlikely(sv->ing_svr_sockfd == -1))
    {
        log_error("socket() error: %s", strerror(errno));
        return -1;
    }
    sockfd_context_t *ing_svr_sk_ctx = malloc(sizeof(sockfd_context_t));
    ing_svr_sk_ctx->sockfd = sv->ing_svr_sockfd;
    ing_svr_sk_ctx->is_server = IS_SERVER_TRUE;
    ing_svr_sk_ctx->peer_svr_fd = -1;

    log_info("Initializing epoll...");
    sv->epfd = epoll_create1(0);
    if (unlikely(sv->epfd == -1))
    {
        log_error("epoll_create1() error: %s", strerror(errno));
        return -1;
    }

    struct epoll_event event;
    event.events = EPOLLIN;

    event.data.ptr = rpc_svr_sk_ctx;
    ret = epoll_ctl(sv->epfd, EPOLL_CTL_ADD, sv->rpc_svr_sockfd, &event);
    if (unlikely(ret == -1))
    {
        log_error("epoll_ctl() error: %s", strerror(errno));
        return -1;
    }

    event.data.ptr = ing_svr_sk_ctx;
    ret = epoll_ctl(sv->epfd, EPOLL_CTL_ADD, sv->ing_svr_sockfd, &event);
    if (unlikely(ret == -1))
    {
        log_error("epoll_ctl() error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/* TODO: Cleanup on errors */
static int server_exit(struct server_vars *sv)
{
    int ret;

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_DEL, sv->rpc_svr_sockfd, NULL);
    if (unlikely(ret == -1))
    {
        log_error("epoll_ctl() error: %s", strerror(errno));
        return -1;
    }

    ret = epoll_ctl(sv->epfd, EPOLL_CTL_DEL, sv->ing_svr_sockfd, NULL);
    if (unlikely(ret == -1))
    {
        log_error("epoll_ctl() error: %s", strerror(errno));
        return -1;
    }

    ret = close(sv->epfd);
    if (unlikely(ret == -1))
    {
        log_error("close() error: %s", strerror(errno));
        return -1;
    }

    ret = close(sv->rpc_svr_sockfd);
    if (unlikely(ret == -1))
    {
        log_error("close() error: %s", strerror(errno));
        return -1;
    }

    ret = close(sv->ing_svr_sockfd);
    if (unlikely(ret == -1))
    {
        log_error("close() error: %s", strerror(errno));
        return -1;
    }

    ret = io_exit();
    if (unlikely(ret == -1))
    {
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

    while (1)
    {
        log_debug("Waiting for new RX events...");
        n_fds = epoll_wait(sv->epfd, event, N_EVENTS_MAX, -1);
        if (unlikely(n_fds == -1))
        {
            log_error("epoll_wait() error: %s", strerror(errno));
            return -1;
        }

        log_debug("epoll_wait() returns %d new events", n_fds);

        for (i = 0; i < n_fds; i++)
        {
            ret = event_process(&event[i], sv);
            if (unlikely(ret == -1))
            {
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

    while (1)
    {
        ret = conn_write(&sockfd);
        if (unlikely(ret == -1))
        {
            log_error("conn_write() error");
            return -1;
        }
        else if (ret == 1)
        {
            continue;
        }

        log_debug("Closing the connection after TX.\n");
        ret = conn_close(sv, sockfd);
        if (unlikely(ret == -1))
        {
            log_error("conn_close() error");
            return -1;
        }
    }

    return 0;
}

static void metrics_collect(void)
{
    while (1)
    {
        sleep(30);
    }
}

static int gateway(void)
{
    const struct rte_memzone *memzone = NULL;
    int NUM_LCORES = 4;
    unsigned int lcore_worker[NUM_LCORES];
    struct server_vars sv;
    int ret;
    memset(peer_node_sockfds, 0, sizeof(peer_node_sockfds));

    fn_id = 0;

    memzone = rte_memzone_lookup(MEMZONE_NAME);
    if (unlikely(memzone == NULL))
    {
        log_error("rte_memzone_lookup() error");
        goto error_0;
    }

    cfg = memzone->addr;

    ret = server_init(&sv);
    if (unlikely(ret == -1))
    {
        log_error("server_init() error");
        goto error_0;
    }

    for (int i = 0; i < NUM_LCORES; ++i) {
        lcore_worker[i] = (i == 0) 
            ? rte_get_next_lcore(rte_get_main_lcore(), 1, 1) 
            : rte_get_next_lcore(lcore_worker[i - 1], 1, 1);

        if (unlikely(lcore_worker[i] == RTE_MAX_LCORE)) {
            log_error("rte_get_next_lcore() error");
            goto error_1;
        }
    }

    ret = rte_eal_remote_launch(server_process_rx, &sv, lcore_worker[0]);
    if (unlikely(ret < 0))
    {
        log_error("rte_eal_remote_launch() error: %s", rte_strerror(-ret));
        goto error_1;
    }

    ret = rte_eal_remote_launch(server_process_tx, &sv, lcore_worker[1]);
    if (unlikely(ret < 0))
    {
        log_error("rte_eal_remote_launch() error: %s", rte_strerror(-ret));
        goto error_1;
    }

    metrics_collect();

    const char *error_messages[] = {
        "server_process_rx() error",
        "server_process_tx() error",
        "rpc_client() error",
        "rpc_server() error"
    };

    for (int i = 0; i < NUM_LCORES; i++) {
        ret = rte_eal_wait_lcore(lcore_worker[i]);
        if (unlikely(ret == -1)) {
            log_error("%s", error_messages[i]);
            goto error_1;
        }
    }

    ret = server_exit(&sv);
    if (unlikely(ret == -1))
    {
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

    // show all logs larger than level DEBUG
    // The level enum is defined in log.h
    log_set_level(LOG_INFO);
    int ret;

    ret = rte_eal_init(argc, argv);
    if (unlikely(ret == -1))
    {
        log_error("rte_eal_init() error: %s", rte_strerror(rte_errno));
        goto error_0;
    }

    ret = gateway();
    if (unlikely(ret == -1))
    {
        log_error("gateway() error");
        goto error_1;
    }

    ret = rte_eal_cleanup();
    if (unlikely(ret < 0))
    {
        log_error("rte_eal_cleanup() error: %s", rte_strerror(-ret));
        goto error_0;
    }

    return 0;

error_1:
    rte_eal_cleanup();
error_0:
    return 1;
}
