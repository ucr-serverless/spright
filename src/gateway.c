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

#define SERVER_PORT 8080
#define PEER_GW_PORT 8083

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

static int get_client_info(int client_socket) {

#ifdef ENABLE_TIMER
	struct timespec t_start;
    struct timespec t_end;

	get_monotonic_time(&t_start);
#endif

	struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
	int client_port;
    
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
    printf("Client address: %s:%d\n", ip_str, client_port);

#ifdef ENABLE_TIMER
	get_monotonic_time(&t_end);
	printf("[%s] execution latency: %ld.\n", __func__, get_elapsed_time_nano(&t_start, &t_end));
#endif

	return client_port;
}

static int rpc_client_setup(char *server_ip, uint16_t server_port,
					  char *client_ip, uint16_t client_port) {
	struct sockaddr_in server_addr, client_addr;
	// ssize_t bytes_sent;
	int sockfd;
	int ret;
	int opt = 1;

	printf("Destination GW Server (%s:%u). Source GW Client (%s:%u)\n",
				server_ip, server_port, client_ip, client_port);

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

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
    client_addr.sin_addr.s_addr = inet_addr(client_ip);

    // Bind the client socket to the specified IP address and port number
    ret = bind(sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr));
    if (ret == -1) {
        fprintf(stderr, "bind() error: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.s_addr = inet_addr(server_ip);

	ret = connect(sockfd, (struct sockaddr *)&server_addr,
	              sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "[%s()] connect() error: %s\n", __func__, strerror(errno));
		return -1;
	}

	// bytes_sent = send(sockfd, txn, sizeof(*txn), 0);
	// if (unlikely(bytes_sent == -1)) {
	// 	fprintf(stderr, "send() error: %s\n", strerror(errno));
	// 	return -1;
	// }

	// ret = close(sockfd);
	// if (unlikely(ret == -1)) {
	// 	fprintf(stderr, "close() error: %s\n", strerror(errno));
	// 	return -1;
	// }

	return sockfd;
}

static int rpc_client_send(int peer_node_idx, struct http_transaction *txn) {
	ssize_t bytes_sent;
	int sockfd = peer_node_sockfds[peer_node_idx];

	bytes_sent = send(sockfd, txn, sizeof(*txn), 0);
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

	int client_port = get_client_info(sockfd);

	/* TODO: Handle incomplete reads */
	if (client_port == PEER_GW_PORT) {
		printf("Receiving from SPRIGHT GW.\n");
		int n = read(sockfd, txn, sizeof(*txn));
		if (unlikely(n == -1)) {
			fprintf(stderr, "read() error: %s\n", strerror(errno));
			goto error_1;
		}

		// Send txn to local function
		printf("\tRoute id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u\n", 
					txn->route_id, txn->hop_count,
					cfg->route[txn->route_id].hop[txn->hop_count],
					txn->next_fn);
		ret = io_tx(txn, cfg->route[txn->route_id].hop[txn->hop_count]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "io_tx() error\n");
			goto error_1;
		}

		return 0;

	} else {
		printf("Receiving from External User.\n");
		txn->length_request = read(sockfd, txn->request, HTTP_MSG_LENGTH_MAX);
		if (unlikely(txn->length_request == -1)) {
			fprintf(stderr, "read() error: %s\n", strerror(errno));
			goto error_1;
		}
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

	ret = io_rx((void **)&txn);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_rx() error\n");
		goto error_0;
	}

	// Inter-node Communication
	if (cfg->route[txn->route_id].hop[txn->hop_count] != fn_id) {
		uint8_t *peer_node_idx = get_node(cfg->route[txn->route_id].hop[txn->hop_count]);
		printf("Destination function is %u on node %u (%s:%u).\n",
				cfg->route[txn->route_id].hop[txn->hop_count], *peer_node_idx,
				cfg->nodes[*peer_node_idx].ip_address, cfg->nodes[*peer_node_idx].port);

		// if (rpc_client(cfg->nodes[*peer_node_idx].ip_address,
		// 			   SERVER_PORT,
		// 			   cfg->nodes[cfg->local_node_idx].ip_address,
		// 			   cfg->nodes[cfg->local_node_idx].port, txn) == -1) {
		// 	fprintf(stderr, "rpc_client() error\n");
		// }

		if (peer_node_sockfds[*peer_node_idx] == 0) {
			peer_node_sockfds[*peer_node_idx] = rpc_client_setup(
					   cfg->nodes[*peer_node_idx].ip_address,
					   SERVER_PORT,
					   cfg->nodes[cfg->local_node_idx].ip_address,
					   cfg->nodes[cfg->local_node_idx].port);
		} else if (peer_node_sockfds[*peer_node_idx] < 0) {
			fprintf(stderr, "Invalid socket error.\n");
		}

		ret = rpc_client_send(*peer_node_idx, txn);

		rte_mempool_put(cfg->mempool, txn);

		return 1;
	}

	txn->hop_count++;

	printf("Next hop is %u\n", cfg->route[txn->route_id].hop[txn->hop_count]);

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

	if (event->data.fd == sv->sockfd) {
		ret = conn_accept(sv);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "conn_accept() error\n");
			return -1;
		}
	} else if (event->events & EPOLLIN) {
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

	printf("Initializing intra-node I/O... \n");
	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		return -1;
	}

	printf("Initializing server socket... \n");
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
	server_addr.sin_port = htons(SERVER_PORT);
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

	printf("Initializing epoll... \n");
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

		printf("Closing the connection after TX.\n");
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
