// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
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

#include <rte_branch_prediction.h>
#include <rte_errno.h>
#include <rte_mempool.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "spright.h"

#include "io.h"

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif /* SYS_pidfd_open */

#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif /* SYS_pidfd_getfd */

#define MAP_NAME "sock_map"

#define PORT_SK_MSG 8081
#define PORT_RPC 8082

struct metadata {
	int node_id;
	void *obj;
};

static int sockfd_sk_msg = -1;

/* TODO: Cleanup on errors */
static void *dummy_server(void* arg)
{
	struct sockaddr_in addr;
	int sockfd_l;
	int sockfd_c;
	int optval;
	int ret;

	sockfd_l = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(sockfd_l == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	optval = 1;
	ret = setsockopt(sockfd_l, SOL_SOCKET, SO_REUSEADDR, &optval,
	                 sizeof(int));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "setsockopt() error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_SK_MSG);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = bind(sockfd_l, (struct sockaddr *)&addr,
	           sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "bind() error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	/* TODO: Correct backlog? */
	ret = listen(sockfd_l, 10);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "listen() error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	while (1) {
		sockfd_c = accept(sockfd_l, NULL, NULL);
		if (unlikely(sockfd_c == -1)) {
			fprintf(stderr, "accept() error: %s\n",
			        strerror(errno));
			pthread_exit(NULL);
		}
	}

	pthread_exit(NULL);
}

/* TODO: Cleanup on errors */
static int rpc_server(int fd_sk_msg_map)
{
	struct sockaddr_in addr;
	ssize_t bytes_received;
	int sockfd_sk_msg_nf;
	int buffer[3];
	int sockfd_l;
	int sockfd_c;
	int optval;
	int pidfd;
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
	addr.sin_port = htons(PORT_RPC);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

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

	for (i = 0; i < cfg->n_nfs; i++) {
		sockfd_c = accept(sockfd_l, NULL, NULL);
		if (unlikely(sockfd_c == -1)) {
			fprintf(stderr, "accept() error: %s\n",
			        strerror(errno));
			return -1;
		}

		bytes_received = recv(sockfd_c, buffer, 3 * sizeof(int), 0);
		if (unlikely(bytes_received == -1)) {
			fprintf(stderr, "recv() error: %s\n", strerror(errno));
			return -1;
		}

		pidfd = syscall(SYS_pidfd_open, buffer[0], 0);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "SYS_pidfd_open() error: %s\n",
			        strerror(errno));
			return -1;
		}

		sockfd_sk_msg_nf = syscall(SYS_pidfd_getfd, pidfd, buffer[1],
		                           0);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "__NR_pidfd_getfd() error: %s\n",
			        strerror(errno));
			return -1;
		}

		ret = bpf_map_update_elem(fd_sk_msg_map, &buffer[2],
		                          &sockfd_sk_msg_nf, 0);
		if (unlikely(ret < 0)) {
			fprintf(stderr, "bpf_map_update_elem() error: %s\n",
			        strerror(-ret));
			return -1;
		}

		printf("%s: NF_ID %d -> SOCKFD %d\n", MAP_NAME, buffer[2],
		       sockfd_sk_msg_nf);

		ret = close(sockfd_c);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}
	}

	ret = close(sockfd_l);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/* TODO: Cleanup on errors */
static int rpc_client(void)
{
	struct sockaddr_in addr;
	ssize_t bytes_sent;
	int buffer[3];
	int sockfd;
	int ret;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(sockfd == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_RPC);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	ret = connect(sockfd, (struct sockaddr *)&addr,
	              sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "connect() error: %s\n", strerror(errno));
		return -1;
	}

	buffer[0] = getpid();
	buffer[1] = sockfd_sk_msg;
	buffer[2] = node_id;

	bytes_sent = send(sockfd, buffer, 3 * sizeof(int), 0);
	if (unlikely(bytes_sent == -1)) {
		fprintf(stderr, "send() error: %s\n", strerror(errno));
		return -1;
	}

	ret = close(sockfd);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "close() error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/* TODO: Cleanup on errors */
static int init_gateway(void)
{
	struct bpf_object* obj = NULL;
	struct sockaddr_in addr;
	int fd_sk_msg_prog;
	int fd_sk_msg_map;
	pthread_t thread;
	int ret;

	ret = pthread_create(&thread, NULL, &dummy_server, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_create() error: %s\n", strerror(ret));
		return -1;
	}

	ret = bpf_prog_load("obj/sk_msg_kern.o", BPF_PROG_TYPE_SK_MSG, &obj,
	                    &fd_sk_msg_prog);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "bpf_prog_load() error: %s\n", strerror(-ret));
		return -1;
	}

	fd_sk_msg_map = bpf_object__find_map_fd_by_name(obj, MAP_NAME);
	if (unlikely(fd_sk_msg_map < 0)) {
		fprintf(stderr, "bpf_object__find_map_fd_by_name() error: %s\n",
		        strerror(-ret));
		return -1;
	}

	ret = bpf_prog_attach(fd_sk_msg_prog, fd_sk_msg_map, BPF_SK_MSG_VERDICT,
	                      0);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "bpf_prog_attach() error: %s\n",
		        strerror(-ret));
		return -1;
	}

	ret = rpc_server(fd_sk_msg_map);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "rpc_server() error\n");
		return -1;
	}

	sockfd_sk_msg = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(sockfd_sk_msg == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_SK_MSG);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	ret = connect(sockfd_sk_msg, (struct sockaddr *)&addr,
	              sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "connect() error: %s\n", strerror(errno));
		return -1;
	}

	ret = bpf_map_update_elem(fd_sk_msg_map, &node_id, &sockfd_sk_msg, 0);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "bpf_map_update_elem() error: %s\n",
		        strerror(-ret));
		return -1;
	}

	return 0;
}

/* TODO: Cleanup on errors */
static int init_nf(void)
{
	struct sockaddr_in addr;
	int ret;

	sockfd_sk_msg = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(sockfd_sk_msg == -1)) {
		fprintf(stderr, "socket() error: %s\n", strerror(errno));
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_SK_MSG);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	ret = connect(sockfd_sk_msg, (struct sockaddr *)&addr,
	              sizeof(struct sockaddr_in));
	if (unlikely(ret == -1)) {
		fprintf(stderr, "connect() error: %s\n", strerror(errno));
		return -1;
	}

	ret = rpc_client();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "rpc_client() error\n");
		return -1;
	}

	return 0;
}

static int exit_gateway(void)
{
	return 0;
}

static int exit_nf(void)
{
	return 0;
}

int io_init(void)
{
	int ret;

	if (node_id == -1) {
		return 0;
	}

	if (node_id == 0) {
		ret = init_gateway();
		if (unlikely(ret == -1)) {
			fprintf(stderr, "init_gateway() error\n");
			return -1;
		}
	} else {
		ret = init_nf();
		if (unlikely(ret == -1)) {
			fprintf(stderr, "init_nf() error\n");
			return -1;
		}
	}

	return 0;
}

int io_exit(void)
{
	int ret;

	if (node_id == -1) {
		return 0;
	}

	if (node_id == 0) {
		ret = exit_gateway();
		if (unlikely(ret == -1)) {
			fprintf(stderr, "exit_gateway() error\n");
			return -1;
		}
	} else {
		ret = exit_nf();
		if (unlikely(ret == -1)) {
			fprintf(stderr, "exit_nf() error\n");
			return -1;
		}
	}

	return 0;
}

int io_rx(void **obj)
{
	ssize_t bytes_received;
	struct metadata m;

	bytes_received = recv(sockfd_sk_msg, &m, sizeof(struct metadata), 0);
	if (unlikely(bytes_received == -1)) {
		fprintf(stderr, "recv() error: %s\n", strerror(errno));
		return -1;
	}

	*obj = m.obj;

	return 0;
}

int io_tx(void *obj, uint8_t next_node)
{
	ssize_t bytes_sent;
	struct metadata m;

	m.node_id = next_node;
	m.obj = obj;

	bytes_sent = send(sockfd_sk_msg, &m, sizeof(struct metadata), 0);
	if (unlikely(bytes_sent == -1)) {
		fprintf(stderr, "send() error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}
