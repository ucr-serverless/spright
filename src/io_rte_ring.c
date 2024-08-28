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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_ring.h>

#include "spright.h"

#include "io.h"

#define RING_NAME_FORMAT "SPRIGHT_RING_%hhu"

#define RING_NAME_LENGTH_MAX 16

#define RING_LENGTH_MAX (1U << 16)

static struct rte_ring **ring = NULL;

static int init_primary(void)
{
    char ring_name[RING_NAME_LENGTH_MAX];
    uint8_t i;
    uint8_t j;

    for (i = 0; i < cfg->n_nfs + 1; i++)
    {
        snprintf(ring_name, RING_NAME_LENGTH_MAX, RING_NAME_FORMAT, i);

        /* TODO: Change "flags" argument */
        ring[i] = rte_ring_create(ring_name, RING_LENGTH_MAX, rte_socket_id(), 0);
        if (unlikely(ring[i] == NULL))
        {
            log_error("rte_ring_create() error: %s", rte_strerror(rte_errno));
            goto error_0;
        }
    }

    return 0;

error_0:
    for (j = 0; j < i; j++)
    {
        rte_ring_free(ring[j]);
    }
    return -1;
}

static int init_secondary(void)
{
    char ring_name[RING_NAME_LENGTH_MAX];
    uint8_t i;

    for (i = 0; i < cfg->n_nfs + 1; i++)
    {
        snprintf(ring_name, RING_NAME_LENGTH_MAX, RING_NAME_FORMAT, i);

        ring[i] = rte_ring_lookup(ring_name);
        if (unlikely(ring[i] == NULL))
        {
            log_error("rte_ring_lookup() error: %s", rte_strerror(rte_errno));
            return -1;
        }
    }

    return 0;
}

static int exit_primary(void)
{
    uint8_t i;

    for (i = 0; i < cfg->n_nfs + 1; i++)
    {
        rte_ring_free(ring[i]);
    }

    return 0;
}

static int exit_secondary(void)
{
    return 0;
}

int io_init(void)
{
    int ret;

    ring = malloc((cfg->n_nfs + 1) * sizeof(struct rte_ring *));
    if (unlikely(ring == NULL))
    {
        log_error("malloc() error: %s", strerror(errno));
        goto error_0;
    }

    if (rte_eal_process_type() == RTE_PROC_PRIMARY)
    {
        ret = init_primary();
        if (unlikely(ret == -1))
        {
            log_error("init_primary() error");
            goto error_1;
        }
    }
    else if (rte_eal_process_type() == RTE_PROC_SECONDARY)
    {
        ret = init_secondary();
        if (unlikely(ret == -1))
        {
            log_error("init_secondary() error");
            goto error_1;
        }
    }
    else
    {
        goto error_1;
    }

    return 0;

error_1:
    free(ring);
error_0:
    return -1;
}

int io_exit(void)
{
    int ret;

    if (rte_eal_process_type() == RTE_PROC_PRIMARY)
    {
        ret = exit_primary();
        if (unlikely(ret == -1))
        {
            log_error("exit_primary() error");
            goto error_0;
        }
    }
    else if (rte_eal_process_type() == RTE_PROC_SECONDARY)
    {
        ret = exit_secondary();
        if (unlikely(ret == -1))
        {
            log_error("exit_secondary() error");
            goto error_0;
        }
    }
    else
    {
        goto error_0;
    }

    free(ring);

    return 0;

error_0:
    free(ring);
    return -1;
}

int io_rx(void **obj)
{
    while (rte_ring_dequeue(ring[fn_id], obj) != 0)
        ;

    return 0;
}

int io_tx(void *obj, uint8_t next_fn)
{
    while (rte_ring_enqueue(ring[next_fn], obj) != 0)
        ;

    return 0;
}
