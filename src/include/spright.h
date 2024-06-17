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
