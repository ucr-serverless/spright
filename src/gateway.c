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
#include "log.h"

#define EXTERNAL_SERVER_PORT 8080
#define INTERNAL_SERVER_PORT 8084

#define BACKLOG (1U << 16)

#define N_EVENTS_MAX (1U << 17)

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
        perror("setsockopt(SO_KEEPALIVE)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Set TCP keep-alive parameters
    optval = 60; // Seconds before sending keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0) {
        perror("setsockopt(TCP_KEEPIDLE)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    optval = 10; // Interval in seconds between keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0) {
        perror("setsockopt(TCP_KEEPINTVL)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    optval = 5; // Number of unacknowledged probes before considering the connection dead
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, optlen) < 0) {
        perror("setsockopt(TCP_KEEPCNT)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
}

static int get_client_info(int client_socket) {

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
        perror("getpeername");
        close(client_socket);
        return -1;
    }
    
    // Convert IP address to human-readable form
    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str)) == NULL) {
        perror("inet_ntop");
        close(client_socket);
        return -1;
    }

	client_port = ntohs(addr.sin_port);
    
    // Print client's IP address and port number
    log_debug("Client address: %s:%d", ip_str, client_port);

#ifdef ENABLE_TIMER
	get_monotonic_time(&t_end);
	log_debug("[%s] execution latency: %ld.", __func__, get_elapsed_time_nano(&t_start, &t_end));
#endif

	return client_port;
}

// Helper function to read exactly count bytes from fd into buf
ssize_t read_full(int fd, void *buf, size_t count) {
    size_t bytes_read = 0;
    ssize_t result;

    while (bytes_read < count) {
        result = read(fd, (char *)buf + bytes_read, count - bytes_read);

        if (result < 0) {
            // Error occurred
            if (errno == EINTR) {
                // Interrupted by signal, continue reading
                continue;
            }
            perror("read");
            return -1;
        } else if (result == 0) {
            // EOF reached
            break;
        }

        bytes_read += result;
    }

    return bytes_read;
}

static int inter_node_server(void) {
	struct sockaddr_in addr;
	int sockfd_l;
	int sockfd_c = 0;
	int optval;
	uint8_t i;
	int ret;

	sockfd_l = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(sockfd_l == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
		return -1;
	}

	optval = 1;
	ret = setsockopt(sockfd_l, SOL_SOCKET, SO_REUSEADDR, &optval,
	                 sizeof(int));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "setsockopt() error: %s\n", strerror(errno));
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(INTERNAL_SERVER_PORT);
	addr.sin_addr.s_addr = inet_addr(cfg->nodes[cfg->local_node_idx].ip_address);

	ret = bind(sockfd_l, (struct sockaddr *)&addr,
	           sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "bind() error: %s\n", strerror(errno));
		return -1;
	}

	/* TODO: Correct backlog? */
	ret = listen(sockfd_l, 10);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "listen() error: %s\n", strerror(errno));
		return -1;
	}

	for (i = 0; i < cfg->n_nodes - 1; i++) {
		sockfd_c = accept(sockfd_l, NULL, NULL);
		if (unlikely(sockfd_c == -1)) {
			fprintf(stderr, "accept() error: %s\n",
			        strerror(errno));
			return -1;
		}

		configure_keepalive(sockfd_c);

	}

	struct http_transaction *txn = NULL;

	/* TODO: Handle multiple peer nodes */
	while(1) {
		ret = rte_mempool_get(cfg->mempool, (void **)&txn);
		if (unlikely(ret < 0)) {
			fprintf(stderr, "rte_mempool_get() error: %s\n",
					rte_strerror(-ret));
			goto error_0;
		}

		get_client_info(sockfd_c);

		log_debug("Receiving from PEER GW.");
        ssize_t total_bytes_received = read_full(sockfd_c, txn, sizeof(*txn));
        if (total_bytes_received == -1) {
            fprintf(stderr, "read_full() error\n");
            goto error_1;
        } else if (total_bytes_received != sizeof(*txn)) {
            fprintf(stderr, "Incomplete transaction received: expected %ld, got %zd\n", sizeof(*txn), total_bytes_received);
            goto error_1;
        }

		log_debug("Bytes received: %zd. \t sizeof(*txn): %ld.", total_bytes_received, sizeof(*txn));

		// Send txn to local function
		log_debug("\tRoute id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u", 
					txn->route_id, txn->hop_count,
					cfg->route[txn->route_id].hop[txn->hop_count],
					txn->next_fn);
		ret = io_tx(txn, cfg->route[txn->route_id].hop[txn->hop_count]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "io_tx() error\n");
			goto error_1;
		}
	}

	ret = close(sockfd_l);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
		return -1;
	}

	return 0;

error_1:
	rte_mempool_put(cfg->mempool, txn);
	close(sockfd_c);
	close(sockfd_l);
error_0:
	return -1;
}

void* inter_node_server_thread(void* arg) {
    int ret = inter_node_server();
    if (unlikely(ret == -1)) {
        fprintf(stderr, "inter_node_server() error\n");
    }
    return NULL;
}

static int rpc_client_setup(char *server_ip, uint16_t server_port) {
	struct sockaddr_in server_addr;
	int sockfd;
	int ret;
	int opt = 1;

	log_debug("Destination GW Server (%s:%u).", server_ip, server_port);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(sockfd == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
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
		fprintf(stderr, "connect() error: %s\n", strerror(errno));
		return -1;
	}

	return sockfd;
}

static int rpc_client_send(int peer_node_idx, struct http_transaction *txn) {
	ssize_t bytes_sent;
	int sockfd = peer_node_sockfds[peer_node_idx];

	bytes_sent = send(sockfd, txn, sizeof(*txn), 0);

	log_debug("peer_node_idx: %d \t bytes_sent: %ld \t sizeof(*txn): %ld", peer_node_idx, bytes_sent, sizeof(*txn));
	if (unlikely(bytes_sent == -1)) {
		fprintf(stderr, "send() error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

// static int rpc_client_close(int peer_node_idx) {

// 	int sockfd = peer_node_sockfds[peer_node_idx];

// 	ret = close(sockfd);
// 	if (unlikely(ret == -1)) {
// 		fprintf(stderr, "close() error: %s\n", strerror(errno));
// 		return -1;
// 	}

// peer_node_sockfds[peer_node_idx] = 0;

// 	return 0;
// }

static int conn_accept(struct server_vars *sv)
{
	struct epoll_event event;
	int sockfd;
	int ret;

	sockfd = accept(sv->sockfd, NULL, NULL);
	if (unlikely(sockfd == -1)) {
		fprintf(stderr, "accept() error: %s\n", strerror(errno));
		goto error_0;
	}

	event.events = EPOLLIN | EPOLLONESHOT;
	event.data.fd = sockfd;

	ret = epoll_ctl(sv->epfd, EPOLL_CTL_ADD, sockfd, &event);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "epoll_ctl() error: %s\n", strerror(errno));
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
		fprintf(stderr, "epoll_ctl() error: %s\n", strerror(errno));
		goto error_1;
	}

	ret = close(sockfd);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
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
		fprintf(stderr, "rte_mempool_get() error: %s\n",
		        rte_strerror(-ret));
		goto error_0;
	}

	get_client_info(sockfd);

	log_debug("Receiving from External User.");
	txn->length_request = read(sockfd, txn->request, HTTP_MSG_LENGTH_MAX);
	if (unlikely(txn->length_request == -1)) {
		fprintf(stderr, "read() error: %s\n", strerror(errno));
		goto error_1;
	}

	txn->sockfd = sockfd;

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
		fprintf(stderr, "io_tx() error\n");
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

	log_debug("Waiting for the next write.", __func__);

	ret = io_rx((void **)&txn);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_rx() error\n");
		goto error_0;
	}

	// Inter-node Communication
	if (cfg->route[txn->route_id].hop[txn->hop_count] != fn_id) {
		uint8_t *peer_node_idx = get_node(cfg->route[txn->route_id].hop[txn->hop_count]);
		log_debug("Destination function is %u on node %u (%s:%u).",
				cfg->route[txn->route_id].hop[txn->hop_count], *peer_node_idx,
				cfg->nodes[*peer_node_idx].ip_address, INTERNAL_SERVER_PORT);

		if (peer_node_sockfds[*peer_node_idx] == 0) {
			log_info("RPC client connects with node %u (%s:%u).",
				*peer_node_idx, cfg->nodes[*peer_node_idx].ip_address,
				INTERNAL_SERVER_PORT);
			peer_node_sockfds[*peer_node_idx] = rpc_client_setup(
					   cfg->nodes[*peer_node_idx].ip_address,
					   INTERNAL_SERVER_PORT);
		} else if (peer_node_sockfds[*peer_node_idx] < 0) {
			fprintf(stderr, "Invalid socket error.\n");
		}

		log_debug("RPC client send message to node %u (%s:%u).",
				*peer_node_idx, cfg->nodes[*peer_node_idx].ip_address,
				INTERNAL_SERVER_PORT);
		ret = rpc_client_send(*peer_node_idx, txn);

		log_debug("rpc_client_send is done.");

		rte_mempool_put(cfg->mempool, txn);

		return 1;
	}

	txn->hop_count++;

	log_debug("Next hop is %u", cfg->route[txn->route_id].hop[txn->hop_count]);

	// Intra-node Communication
	if (txn->hop_count < cfg->route[txn->route_id].length) {
		ret = io_tx(txn,
		            cfg->route[txn->route_id].hop[txn->hop_count]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "io_tx() error\n");
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
		fprintf(stderr, "write() error: %s\n", strerror(errno));
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
			fprintf(stderr, "conn_accept() error\n");
			return -1;
		}
	} else if (event->events & EPOLLIN) {
		log_debug("Reading New Data.", __func__);
		ret = conn_read(event->data.fd);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "conn_read() error\n");
			return -1;
		}

		if (ret == 1) {
			event->events |= EPOLLONESHOT;

			ret = epoll_ctl(sv->epfd, EPOLL_CTL_MOD, event->data.fd,
			                event);
			if (unlikely(ret == -1)) {
				fprintf(stderr, "epoll_ctl() error: %s\n",
				        strerror(errno));
				return -1;
			}
		}
	} else if (event->events & (EPOLLERR | EPOLLHUP)) {
		/* TODO: Handle (EPOLLERR | EPOLLHUP) */
		fprintf(stderr, "(EPOLLERR | EPOLLHUP)");

		log_debug("Error - Close the connection.", __func__);
		ret = conn_close(sv, event->data.fd);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "conn_close() error\n");
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
	pthread_t inter_node_svr_thread;

	log_info("Initializing intra-node I/O...");
	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		return -1;
	}

	ret = pthread_create(&inter_node_svr_thread, NULL, &inter_node_server_thread, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_create() error: %s\n", strerror(ret));
		return -1;
	}

	log_info("Initializing server socket...");
	sv->sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (unlikely(sv->sockfd == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
		return -1;
	}

	optval = 1;
	ret = setsockopt(sv->sockfd, SOL_SOCKET, SO_REUSEADDR, &optval,
	                 sizeof(int));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "setsockopt() error: %s\n", strerror(errno));
		return -1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(EXTERNAL_SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = bind(sv->sockfd, (struct sockaddr *)&server_addr,
	           sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "bind() error: %s\n", strerror(errno));
		return -1;
	}

	ret = listen(sv->sockfd, BACKLOG);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "listen() error: %s\n", strerror(errno));
		return -1;
	}

	log_info("Initializing epoll...");
	sv->epfd = epoll_create1(0);
	if (unlikely(sv->epfd == -1)) {
		fprintf(stderr, "epoll_create1() error: %s\n", strerror(errno));
		return -1;
	}

	event.events = EPOLLIN;
	event.data.fd = sv->sockfd;

	ret = epoll_ctl(sv->epfd, EPOLL_CTL_ADD, sv->sockfd, &event);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "epoll_ctl() error: %s\n", strerror(errno));
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
		fprintf(stderr, "epoll_ctl() error: %s\n", strerror(errno));
		return -1;
	}

	ret = close(sv->epfd);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
		return -1;
	}

	ret = close(sv->sockfd);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
		return -1;
	}

	/* TODO: Move to gateway.c */
	ret = io_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_exit() error\n");
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
			fprintf(stderr, "epoll_wait() error: %s\n",
			        strerror(errno));
			return -1;
		}

		log_debug("%d NEW EVENTS READY =======", n_fds);

		for (i = 0; i < n_fds; i++) {
			ret = event_process(&event[i], sv);
			if (unlikely(ret == -1)) {
				fprintf(stderr, "event_process() error\n");
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
			fprintf(stderr, "conn_write() error\n");
			return -1;
		} else if (ret == 1) {
			continue;
		}

		log_debug("Closing the connection after TX.\n");
		ret = conn_close(sv, sockfd);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "conn_close() error\n");
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
		fprintf(stderr, "rte_memzone_lookup() error\n");
		goto error_0;
	}

	cfg = memzone->addr;

	ret = server_init(&sv);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "server_init() error\n");
		goto error_0;
	}

	lcore_worker[0] = rte_get_next_lcore(rte_get_main_lcore(), 1, 1);
	if (unlikely(lcore_worker[0] == RTE_MAX_LCORE)) {
		fprintf(stderr, "rte_get_next_lcore() error\n");
		goto error_1;
	}

	lcore_worker[1] = rte_get_next_lcore(lcore_worker[0], 1, 1);
	if (unlikely(lcore_worker[1] == RTE_MAX_LCORE)) {
		fprintf(stderr, "rte_get_next_lcore() error\n");
		goto error_1;
	}

	ret = rte_eal_remote_launch(server_process_rx, &sv, lcore_worker[0]);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_remote_launch() error: %s\n",
		        rte_strerror(-ret));
		goto error_1;
	}

	ret = rte_eal_remote_launch(server_process_tx, &sv, lcore_worker[1]);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_remote_launch() error: %s\n",
		        rte_strerror(-ret));
		goto error_1;
	}

	metrics_collect();

	ret = rte_eal_wait_lcore(lcore_worker[0]);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "server_process_rx() error\n");
		goto error_1;
	}

	ret = rte_eal_wait_lcore(lcore_worker[1]);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "server_process_tx() error\n");
		goto error_1;
	}

	ret = server_exit(&sv);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "server_exit() error\n");
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
	log_set_level(LOG_TRACE);

	int ret;

	ret = rte_eal_init(argc, argv);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "rte_eal_init() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_0;
	}

	ret = gateway();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "gateway() error\n");
		goto error_1;
	}

	ret = rte_eal_cleanup();
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_cleanup() error: %s\n",
		        rte_strerror(-ret));
		goto error_0;
	}

	return 0;

error_1:
	rte_eal_cleanup();
error_0:
	return 1;
}
