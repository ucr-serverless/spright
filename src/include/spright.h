/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef SPRIGHT_H
#define SPRIGHT_H

#include <stdint.h>

#include <rte_mempool.h>

#define MEMZONE_NAME "SPRIGHT_MEMZONE"

int node_id;

struct {
	struct rte_mempool *mempool;

	char name[64];

	uint8_t n_nfs;
	struct {
		char name[64];

		uint8_t n_threads;

		struct {
			uint8_t memory_mb;
			uint32_t sleep_ns;
			uint32_t compute;
		} param;
	} nf[UINT8_MAX + 1];

	uint8_t n_routes;
	struct {
		char name[64];

		uint8_t length;
		uint8_t node[UINT8_MAX + 1];
	} route[UINT8_MAX + 1];
} *cfg;

#endif /* SPRIGHT_H */
