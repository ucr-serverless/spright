/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

int io_init(void);

int io_exit(void);

int io_rx(void **obj);

int io_tx(void *obj, uint8_t next_node);

#endif /* IO_H */
