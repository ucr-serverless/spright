// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>

#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_memzone.h>

#include "http.h"
#include "io.h"
#include "spright.h"

static int pipefd_rx[UINT8_MAX][2];
static int pipefd_tx[UINT8_MAX][2];

static int autoscale_memory(uint8_t mb)
{
	char *buffer = NULL;

	if (unlikely(mb == 0)) {
		return 0;
	}

	buffer = malloc(1000000 * mb * sizeof(char));
	if (unlikely(buffer == NULL)) {
		fprintf(stderr, "malloc() error: %s\n", strerror(errno));
		return -1;
	}

	buffer[0] = 'a';
	buffer[1000000 * mb - 1] = 'a';

	free(buffer);

	return 0;
}

static int autoscale_sleep(uint32_t ns) {
	struct timespec interval;
	int ret;

	interval.tv_sec = ns / 1000000000;
	interval.tv_nsec = ns % 1000000000;

	ret = nanosleep(&interval, NULL);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "nanosleep() error: %s\n", rte_strerror(errno));
		return -1;
	}

	return 0;
}

static int autoscale_compute(uint32_t n) {
	uint32_t i;

	for (i = 2; i < sqrt(n); i++) {
		if (n % i == 0) {
			break;
		}
	}

	return 0;
}

static void *nf_worker(void *arg)
{
	struct http_transaction *txn = NULL;
	ssize_t bytes_written;
	ssize_t bytes_read;
	uint8_t index;
	int ret;

	/* TODO: Careful with this pointer as it may point to a stack */
	index = (uint64_t)arg;

	while (1) {
		bytes_read = read(pipefd_rx[index][0], &txn,
		                  sizeof(struct http_transaction *));
		if (unlikely(bytes_read == -1)) {
			fprintf(stderr, "read() error: %s\n", strerror(errno));
			return NULL;
		}

		ret = autoscale_memory(cfg->nf[node_id - 1].param.memory_mb);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "autoscale_memory() error\n");
			return NULL;
		}

		ret = autoscale_sleep(cfg->nf[node_id - 1].param.sleep_ns);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "autoscale_sleep() error\n");
			return NULL;
		}

		ret = autoscale_compute(cfg->nf[node_id - 1].param.compute);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "autoscale_compute() error\n");
			return NULL;
		}

		bytes_written = write(pipefd_tx[index][1], &txn,
		                      sizeof(struct http_transaction *));
		if (unlikely(bytes_written == -1)) {
			fprintf(stderr, "write() error: %s\n", strerror(errno));
			return NULL;
		}
	}

	return NULL;
}

static void *nf_rx(void *arg)
{
	struct http_transaction *txn = NULL;
	ssize_t bytes_written;
	uint8_t i;
	int ret;

	for (i = 0; ; i = (i + 1) % cfg->nf[node_id - 1].n_threads) {
		ret = io_rx((void **)&txn);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "io_rx() error\n");
			return NULL;
		}

		bytes_written = write(pipefd_rx[i][1], &txn,
		                      sizeof(struct http_transaction *));
		if (unlikely(bytes_written == -1)) {
			fprintf(stderr, "write() error: %s\n", strerror(errno));
			return NULL;
		}
	}

	return NULL;
}

static void *nf_tx(void *arg)
{
	struct epoll_event event[UINT8_MAX]; /* TODO: Use Macro */
	struct http_transaction *txn = NULL;
	ssize_t bytes_read;
	uint8_t next_node;
	uint8_t i;
	int n_fds;
	int epfd;
	int ret;

	epfd = epoll_create1(0);
	if (unlikely(epfd == -1)) {
		fprintf(stderr, "epoll_create1() error: %s\n", strerror(errno));
		return NULL;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = fcntl(pipefd_tx[i][0], F_SETFL, O_NONBLOCK);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "fcntl() error: %s\n", strerror(errno));
			return NULL;
		}

		event[0].events = EPOLLIN;
		event[0].data.fd = pipefd_tx[i][0];

		ret = epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd_tx[i][0],
		                &event[0]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "epoll_ctl() error: %s\n",
			        strerror(errno));
			return NULL;
		}
	}

	while (1) {
		n_fds = epoll_wait(epfd, event, cfg->nf[node_id - 1].n_threads,
		                   -1);
		if (unlikely(n_fds == -1)) {
			fprintf(stderr, "epoll_wait() error: %s\n",
			        strerror(errno));
			return NULL;
		}

		for (i = 0; i < n_fds; i++) {
			bytes_read = read(event[i].data.fd, &txn,
			                  sizeof(struct http_transaction *));
			if (unlikely(bytes_read == -1)) {
				fprintf(stderr, "read() error: %s\n",
				        strerror(errno));
				return NULL;
			}

			txn->hop_count++;

			if (likely(txn->hop_count <
			           cfg->route[txn->route_id].length)) {
				next_node =
				cfg->route[txn->route_id].node[txn->hop_count];
			} else {
				next_node = 0;
			}

			ret = io_tx(txn, next_node);
			if (unlikely(ret == -1)) {
				fprintf(stderr, "io_tx() error\n");
				return NULL;
			}
		}
	}

	return NULL;
}

/* TODO: Cleanup on errors */
static int nf(uint8_t nf_id)
{
	const struct rte_memzone *memzone = NULL;
	pthread_t thread_worker[UINT8_MAX];
	pthread_t thread_rx;
	pthread_t thread_tx;
	uint8_t i;
	int ret;

	node_id = nf_id;

	memzone = rte_memzone_lookup(MEMZONE_NAME);
	if (unlikely(memzone == NULL)) {
		fprintf(stderr, "rte_memzone_lookup() error\n");
		return -1;
	}

	cfg = memzone->addr;

	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		return -1;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = pipe(pipefd_rx[i]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "pipe() error: %s\n", strerror(errno));
			return -1;
		}

		ret = pipe(pipefd_tx[i]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "pipe() error: %s\n", strerror(errno));
			return -1;
		}
	}

	ret = pthread_create(&thread_rx, NULL, &nf_rx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_create() error: %s\n", strerror(ret));
		return -1;
	}

	ret = pthread_create(&thread_tx, NULL, &nf_tx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_create() error: %s\n", strerror(ret));
		return -1;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = pthread_create(&thread_worker[i], NULL, &nf_worker,
		                     (void *)(uint64_t)i);
		if (unlikely(ret != 0)) {
			fprintf(stderr, "pthread_create() error: %s\n",
			        strerror(ret));
			return -1;
		}
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = pthread_join(thread_worker[i], NULL);
		if (unlikely(ret != 0)) {
			fprintf(stderr, "pthread_join() error: %s\n",
			        strerror(ret));
			return -1;
		}
	}

	ret = pthread_join(thread_rx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_join() error: %s\n", strerror(ret));
		return -1;
	}

	ret = pthread_join(thread_tx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_join() error: %s\n", strerror(ret));
		return -1;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = close(pipefd_rx[i][0]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}

		ret = close(pipefd_rx[i][1]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}

		ret = close(pipefd_tx[i][0]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}

		ret = close(pipefd_tx[i][1]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}
	}

	ret = io_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_exit() error\n");
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	uint8_t nf_id;
	int ret;

	ret = rte_eal_init(argc, argv);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "rte_eal_init() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_0;
	}

	argc -= ret;
	argv += ret;

	if (unlikely(argc == 1)) {
		fprintf(stderr, "Network Function ID not provided\n");
		goto error_1;
	}

	errno = 0;
	nf_id = strtol(argv[1], NULL, 10);
	if (unlikely(errno != 0 || nf_id < 1)) {
		fprintf(stderr, "Invalid value for Network Function ID\n");
		goto error_1;
	}

	ret = nf(nf_id);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "nf() error\n");
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
