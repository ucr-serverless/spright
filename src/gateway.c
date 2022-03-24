// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
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

#define PORT 8080

#define BACKLOG 1024

#define N_EVENTS_MAX 2048

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

	/* TODO: Handle incomplete reads */
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
		txn->route_id = strtol(string, NULL, 10);
		if (unlikely(errno != 0 || txn->route_id < 0)) {
			txn->route_id = 0;
		}
	}

	txn->hop_count = 0;

	ret = io_tx(txn, cfg->route[txn->route_id].node[0]);
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

	txn->hop_count++;

	if (txn->hop_count < cfg->route[txn->route_id].length) {
		ret = io_tx(txn,
		            cfg->route[txn->route_id].node[txn->hop_count]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "io_tx() error\n");
			goto error_1;
		}

		return 1;
	}

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

	/* TODO: Move to gateway.c */
	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		return -1;
	}

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
	server_addr.sin_port = htons(PORT);
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

	node_id = 0;

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
