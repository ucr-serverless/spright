/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdint.h>

int gateway_init(int argc, char **argv, uint32_t server_port);

int gateway_exit(void);

#endif /* GATEWAY_H */
