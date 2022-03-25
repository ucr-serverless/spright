// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package main

// #cgo pkg-config: libdpdk
//
// #include <stdlib.h>
//
// #include <rte_eal.h>
// #include <rte_errno.h>
// #include <rte_memzone.h>
//
// static char **argv_create(int argc)
// {
// 	char **argv = NULL;
//
// 	argv = malloc(argc * sizeof(char *));
// 	if (argv == NULL) {
// 		fprintf(stderr, "malloc() error: %s\n", strerror(errno));
// 		return NULL;
// 	}
//
//	return argv;
// }
//
// static void argv_destroy(char **argv)
// {
// 	free(argv);
// }
import "C"

import (
	"fmt"
	"os"
)

func init() {
	argc := C.int(len(os.Args))
	argv := C.argv_create(argc)

	ret := C.rte_eal_init(argc, argv)
	if (ret == -1) {
		fmt.Fprintf(os.Stderr, "rte_eal_init() error: " +
		                       C.GoString(C.rte_strerror(C.rte_errno)) +
		                       "\n")
	}

	C.argv_destroy(argv)
}

func cleanup() {
	ret = C.rte_eal_cleanup()
	if (ret < 0) {
		fmt.Fprintf(os.Stderr, "rte_eal_cleanup() error: " +
		                       C.GoString(C.rte_strerror(-ret)) + "\n")
	}
}

func main() {
	init()
	cleanup()
}
