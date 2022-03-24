// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libconfig.h>

#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_memzone.h>
#include <rte_mempool.h>

#include "http.h"
#include "io.h"
#include "spright.h"

#define MEMPOOL_NAME "SPRIGHT_MEMPOOL"

#define N_MEMPOOL_ELEMENTS 1024

static void cfg_print(void)
{
	uint8_t i;
	uint8_t j;

	printf("Number of NFs: %hhu\n", cfg->n_nfs);
	printf("NFs:\n");
	for (i = 0; i < cfg->n_nfs; i++) {
		printf("\tID: %hhu\n", i + 1);
		printf("\tName: %s\n", cfg->nf[i].name);
		printf("\tNumber of Threads: %hhu\n", cfg->nf[i].n_threads);
		printf("\tParams:\n");
		printf("\t\tmemory_mb: %hhu\n", cfg->nf[i].param.memory_mb);
		printf("\t\tsleep_ns: %u\n", cfg->nf[i].param.sleep_ns);
		printf("\t\tcompute: %u\n", cfg->nf[i].param.compute);
		printf("\n");
	}

	printf("Number of Routes: %hhu\n", cfg->n_routes);
	printf("Routes:\n");
	for (i = 0; i < cfg->n_routes; i++) {
		printf("\tID: %hhu\n", i + 1);
		printf("\tName: %s\n", cfg->route[i].name);
		printf("\tLength = %hhu\n", cfg->route[i].length);
		printf("\tNodes = [");
		for (j = 0; j < cfg->route[i].length; j++) {
			printf("%hhu ", cfg->route[i].node[j]);
		}
		printf("\b]\n");
		printf("\n");
	}
}

static int cfg_init(char *cfg_file)
{
	config_setting_t *subsubsetting = NULL;
	config_setting_t *subsetting = NULL;
	config_setting_t *setting = NULL;
	const char *name = NULL;
	config_t config;
	int value;
	int ret;
	int id;
	int n;
	int m;
	int i;
	int j;

	/* TODO: Change "flags" argument */
	cfg->mempool = rte_mempool_create(MEMPOOL_NAME, N_MEMPOOL_ELEMENTS,
	                                  sizeof(struct http_transaction), 0, 0,
	                                  NULL, NULL, NULL, NULL,
	                                  rte_socket_id(), 0);
	if (unlikely(cfg->mempool == NULL)) {
		fprintf(stderr, "rte_mempool_create() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_0;
	}

	config_init(&config);

	ret = config_read_file(&config, cfg_file);
	if (unlikely(ret == CONFIG_FALSE)) {
		fprintf(stderr, "config_read_file() error: line %d: %s\n",
		        config_error_line(&config), config_error_text(&config));
		goto error_1;
	}

	setting = config_lookup(&config, "nfs");
	if (unlikely(setting == NULL)) {
		/* TODO: Error message */
		goto error_1;
	}

	ret = config_setting_is_list(setting);
	if (unlikely(ret == CONFIG_FALSE)) {
		/* TODO: Error message */
		goto error_1;
	}

	n = config_setting_length(setting);
	cfg->n_nfs = n;

	for (i = 0; i < n; i++) {
		subsetting = config_setting_get_elem(setting, i);
		if (unlikely(subsetting == NULL)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_is_group(subsetting);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_lookup_int(subsetting, "id", &id);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_lookup_string(subsetting, "name", &name);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		strcpy(cfg->nf[id - 1].name, name);

		ret = config_setting_lookup_int(subsetting, "n_threads",
		                                &value);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		cfg->nf[id - 1].n_threads = value;

		subsubsetting = config_setting_lookup(subsetting, "params");
		if (unlikely(subsubsetting == NULL)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_is_group(subsubsetting);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_lookup_int(subsubsetting, "memory_mb",
		                                &value);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		cfg->nf[id - 1].param.memory_mb = value;

		ret = config_setting_lookup_int(subsubsetting, "sleep_ns",
		                                &value);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		cfg->nf[id - 1].param.sleep_ns = value;

		ret = config_setting_lookup_int(subsubsetting, "compute",
		                                &value);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		cfg->nf[id - 1].param.compute = value;
	}

	setting = config_lookup(&config, "routes");
	if (unlikely(setting == NULL)) {
		/* TODO: Error message */
		goto error_1;
	}

	ret = config_setting_is_list(setting);
	if (unlikely(ret == CONFIG_FALSE)) {
		/* TODO: Error message */
		goto error_1;
	}

	n = config_setting_length(setting);
	cfg->n_routes = n;

	for (i = 0; i < n; i++) {
		subsetting = config_setting_get_elem(setting, i);
		if (unlikely(subsetting == NULL)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_is_group(subsetting);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_lookup_int(subsetting, "id", &id);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_lookup_string(subsetting, "name", &name);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		strcpy(cfg->route[id - 1].name, name);

		subsubsetting = config_setting_lookup(subsetting, "nodes");
		if (unlikely(subsubsetting == NULL)) {
			/* TODO: Error message */
			goto error_1;
		}

		ret = config_setting_is_array(subsubsetting);
		if (unlikely(ret == CONFIG_FALSE)) {
			/* TODO: Error message */
			goto error_1;
		}

		m = config_setting_length(subsubsetting);
		cfg->route[id - 1].length = m;

		for (j = 0; j < m; j++) {
			value = config_setting_get_int_elem(subsubsetting, j);
			cfg->route[id - 1].node[j] = value;
		}
	}

	config_destroy(&config);

	cfg_print();

	return 0;

error_1:
	config_destroy(&config);
	rte_mempool_free(cfg->mempool);
error_0:
	return -1;
}

static int cfg_exit(void)
{
	rte_mempool_free(cfg->mempool);

	return 0;
}

static int shm_mgr(char *cfg_file)
{
	const struct rte_memzone *memzone = NULL;
	int ret;

	node_id = -1;

	memzone = rte_memzone_reserve(MEMZONE_NAME, sizeof(*cfg),
	                              rte_socket_id(), 0);
	if (unlikely(memzone == NULL)) {
		fprintf(stderr, "rte_memzone_reserve() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_0;
	}

	memset(memzone->addr, 0U, sizeof(*cfg));

	cfg = memzone->addr;

	ret = cfg_init(cfg_file);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "cfg_init() error\n");
		goto error_1;
	}

	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		goto error_2;
	}

	/* TODO: Exit loop on interrupt */
	while (1) {
		sleep(30);
	}

	ret = io_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_exit() error\n");
		goto error_2;
	}

	ret = cfg_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "cfg_exit() error\n");
		goto error_1;
	}

	ret = rte_memzone_free(memzone);
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_memzone_free() error: %s\n",
		        rte_strerror(-ret));
		goto error_0;
	}

	return 0;

error_2:
	cfg_exit();
error_1:
	rte_memzone_free(memzone);
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

	argc -= ret;
	argv += ret;

	if (unlikely(argc == 1)) {
		fprintf(stderr, "Configuration file not provided\n");
		goto error_1;
	}

	ret = shm_mgr(argv[1]);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "shm_mgr() error\n");
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
