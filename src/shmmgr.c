// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_memzone.h>
#include <rte_mempool.h>

#include "http.h"
#include "io.h"
#include "spright.h"

#include "shmmgr.h"

#define MEMPOOL_NAME_HTTP "SPRIGHT_MEMPOOL"

#define N_MEMPOOL_ELEMENTS (1U << 16)

static const struct rte_memzone *memzone = NULL;

int shmmgr_init(int argc, char **argv, uint8_t n_nodes,
                unsigned app_transaction_size)
{
	int ret;

	ret = rte_eal_init(argc, argv);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "rte_eal_init() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_0;
	}

	memzone = rte_memzone_reserve(MEMZONE_NAME, sizeof(*cfg),
	                              rte_socket_id(), 0);
	if (unlikely(memzone == NULL)) {
		fprintf(stderr, "rte_memzone_reserve() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_1;
	}

	memset(memzone->addr, 0U, sizeof(*cfg));

	cfg = memzone->addr;

	cfg->n_nodes = n_nodes;

	/* TODO: Change "flags" argument */
	cfg->mempool = rte_mempool_create(MEMPOOL_NAME_HTTP, N_MEMPOOL_ELEMENTS,
	                                  sizeof(uint8_t) +
	                                  sizeof(struct http_transaction) +
	                                  app_transaction_size, 0, 0, NULL,
	                                  NULL, NULL, NULL, rte_socket_id(), 0);
	if (unlikely(cfg->mempool == NULL)) {
		fprintf(stderr, "rte_mempool_create() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_2;
	}

	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		goto error_3;
	}

	return 0;

error_3:
	rte_mempool_free(cfg->mempool);
	cfg->mempool = NULL;
error_2:
	rte_memzone_free(memzone);
	memzone = NULL;
error_1:
	rte_eal_cleanup();
error_0:
	return -1;
}

int shmmgr_exit(void)
{
	int ret;

	ret = io_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_exit() error\n");
		goto error_2;
	}

	rte_mempool_free(cfg->mempool);
	cfg->mempool = NULL;

	ret = rte_memzone_free(memzone);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_memzone_free() error: %s\n",
		        rte_strerror(-ret));
		goto error_1;
	}
	memzone = NULL;

	ret = rte_eal_cleanup();
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_cleanup() error: %s\n",
		        rte_strerror(-ret));
		goto error_0;
	}

	return 0;

error_2:
	rte_mempool_free(cfg->mempool);
	cfg->mempool = NULL;
	rte_memzone_free(memzone);
	memzone = NULL;
error_1:
	rte_eal_cleanup();
error_0:
	return -1;
}
