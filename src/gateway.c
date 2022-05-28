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

#define BACKLOG (1U << 16)

#define N_EVENTS_MAX (1U << 17)

struct {
	int sockfd;
	int epfd;
} server_vars;

static int conn_accept(void)
{
	struct epoll_event event;
	int sockfd;
	int ret;

	sockfd = accept(server_vars.sockfd, NULL, NULL);
	if (unlikely(sockfd == -1)) {
		fprintf(stderr, "accept() error: %s\n", strerror(errno));
		goto error_0;
	}

	event.events = EPOLLIN | EPOLLONESHOT;
	event.data.fd = sockfd;

	ret = epoll_ctl(server_vars.epfd, EPOLL_CTL_ADD, sockfd, &event);
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

static int conn_close(int sockfd)
{
	int ret;

	ret = epoll_ctl(server_vars.epfd, EPOLL_CTL_DEL, sockfd, NULL);
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
	int ret;

	ret = rte_mempool_get(cfg->mempool, (void **)&txn);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_mempool_get() error: %s\n",
		        rte_strerror(-ret));
		goto error_0;
	}
	txn = (void *)txn + sizeof(uint8_t);

	/* TODO: Handle incomplete reads (see EPOLLONESHOT) */
	txn->length_request = read(sockfd, txn->request, HTTP_MSG_LENGTH_MAX);
	if (unlikely(txn->length_request == -1)) {
		fprintf(stderr, "read() error: %s\n", strerror(errno));
		goto error_1;
	}

	txn->sockfd = sockfd;

	ret = io_tx(txn, 1);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_tx() error\n");
		goto error_1;
	}

	return 0;

error_1:
	txn = (void *)txn - sizeof(uint8_t);
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

	*sockfd = txn->sockfd;

	/* TODO: Handle incomplete writes (see EPOLLONESHOT) */
	bytes_sent = write(*sockfd, txn->response, txn->length_response);
	if (unlikely(bytes_sent == -1)) {
		fprintf(stderr, "write() error: %s\n", strerror(errno));
		goto error_1;
	}

	txn = (void *)txn - sizeof(uint8_t);
	rte_mempool_put(cfg->mempool, txn);

	return 0;

error_1:
	txn = (void *)txn - sizeof(uint8_t);
	rte_mempool_put(cfg->mempool, txn);
error_0:
	return -1;
}

static int event_process(struct epoll_event *event)
{
	int ret;

	if (event->data.fd == server_vars.sockfd) {
		ret = conn_accept();
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
	} else if (unlikely(event->events & (EPOLLERR | EPOLLHUP))) {
		/* TODO: Handle (EPOLLERR | EPOLLHUP) */
		fprintf(stderr, "(EPOLLERR | EPOLLHUP)");

		ret = conn_close(event->data.fd);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "conn_close() error\n");
			return -1;
		}
	}

	return 0;
}


/* TODO: Cleanup on errors */
static int server_init(uint32_t port)
{
	struct sockaddr_in server_addr;
	struct epoll_event event;
	int optval;
	int ret;

	server_vars.sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (unlikely(server_vars.sockfd == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
		return -1;
	}

	optval = 1;
	ret = setsockopt(server_vars.sockfd, SOL_SOCKET, SO_REUSEADDR, &optval,
	                 sizeof(int));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "setsockopt() error: %s\n", strerror(errno));
		return -1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = bind(server_vars.sockfd, (struct sockaddr *)&server_addr,
	           sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "bind() error: %s\n", strerror(errno));
		return -1;
	}

	ret = listen(server_vars.sockfd, BACKLOG);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "listen() error: %s\n", strerror(errno));
		return -1;
	}

	server_vars.epfd = epoll_create1(0);
	if (unlikely(server_vars.epfd == -1)) {
		fprintf(stderr, "epoll_create1() error: %s\n", strerror(errno));
		return -1;
	}

	event.events = EPOLLIN;
	event.data.fd = server_vars.sockfd;

	ret = epoll_ctl(server_vars.epfd, EPOLL_CTL_ADD, server_vars.sockfd,
	                &event);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "epoll_ctl() error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/* TODO: Cleanup on errors */
static int server_exit(void)
{
	int ret;

	ret = epoll_ctl(server_vars.epfd, EPOLL_CTL_DEL, server_vars.sockfd,
	                NULL);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "epoll_ctl() error: %s\n", strerror(errno));
		return -1;
	}

	ret = close(server_vars.epfd);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
		return -1;
	}

	ret = close(server_vars.sockfd);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int server_process_rx(void *arg)
{
	struct epoll_event event[N_EVENTS_MAX];
	int n_fds;
	int ret;
	int i;

	while (1) {
		n_fds = epoll_wait(server_vars.epfd, event, N_EVENTS_MAX, -1);
		if (unlikely(n_fds == -1)) {
			fprintf(stderr, "epoll_wait() error: %s\n",
			        strerror(errno));
			return -1;
		}

		for (i = 0; i < n_fds; i++) {
			ret = event_process(&event[i]);
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
	int sockfd;
	int ret;

	while (1) {
		ret = conn_write(&sockfd);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "conn_write() error\n");
			return -1;
		}

		ret = conn_close(sockfd);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "conn_close() error\n");
			return -1;
		}
	}

	return 0;
}

int gateway_init(int argc, char **argv, uint32_t server_port)
{
	const struct rte_memzone *memzone = NULL;
	unsigned int lcore_worker[2];
	int ret;

	ret = rte_eal_init(argc, argv);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "rte_eal_init() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_0;
	}

	node_id = 0;

	memzone = rte_memzone_lookup(MEMZONE_NAME);
	if (unlikely(memzone == NULL)) {
		fprintf(stderr, "rte_memzone_lookup() error\n");
		goto error_1;
	}

	cfg = memzone->addr;

	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		goto error_1;
	}

	ret = server_init(server_port);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "server_init() error\n");
		goto error_2;
	}

	lcore_worker[0] = rte_get_next_lcore(rte_get_main_lcore(), 1, 1);
	if (unlikely(lcore_worker[0] == RTE_MAX_LCORE)) {
		fprintf(stderr, "rte_get_next_lcore() error\n");
		goto error_3;
	}

	lcore_worker[1] = rte_get_next_lcore(lcore_worker[0], 1, 1);
	if (unlikely(lcore_worker[1] == RTE_MAX_LCORE)) {
		fprintf(stderr, "rte_get_next_lcore() error\n");
		goto error_3;
	}

	ret = rte_eal_remote_launch(server_process_rx, NULL, lcore_worker[0]);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_remote_launch() error: %s\n",
		        rte_strerror(-ret));
		goto error_3;
	}

	ret = rte_eal_remote_launch(server_process_tx, NULL, lcore_worker[1]);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_remote_launch() error: %s\n",
		        rte_strerror(-ret));
		goto error_3;
	}

	return 0;

error_3:
	server_exit();
error_2:
	io_exit();
error_1:
	rte_eal_cleanup();
error_0:
	return -1;
}

int gateway_exit(void)
{
	int ret;

	ret = server_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "server_exit() error\n");
		goto error_2;
	}

	ret = io_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_exit() error\n");
		goto error_1;
	}

	ret = rte_eal_cleanup();
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_cleanup() error: %s\n",
		        rte_strerror(-ret));
		goto error_0;
	}

	return 0;

error_2:
	io_exit();
error_1:
	rte_eal_cleanup();
error_0:
	return -1;
}
