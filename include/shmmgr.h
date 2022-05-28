/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef SHMMGR_H
#define SHMMGR_H

#include <stdint.h>

int shmmgr_init(int argc, char **argv, uint8_t n_nodes,
                unsigned size_app_transaction);

int shmmgr_exit(void);

#endif /* SHMMGR_H */
