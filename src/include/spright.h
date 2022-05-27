/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef SPRIGHT_H
#define SPRIGHT_H

#include <stdint.h>

#include <rte_mempool.h>

#define MEMZONE_NAME "SPRIGHT_MEMZONE"

uint8_t node_id = UINT8_MAX;

struct {
	struct rte_mempool *mempool_http;
	struct rte_mempool *mempool_app;

	uint8_t n_nodes;
} *cfg;

#endif /* SPRIGHT_H */
