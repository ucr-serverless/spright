// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package gateway

// #cgo pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}/../../include
// #cgo LDFLAGS: -L${SRCDIR}/../../lib
// #cgo rte_ring LDFLAGS: -l:gateway_rte_ring.a
// #cgo sk_msg LDFLAGS: -l:gateway_sk_msg.a -lbpf
//
// #include <errno.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
//
// #include <rte_branch_prediction.h>
//
// #include <gateway.h>
//
// static void *argv_create(int argc)
// {
// 	char *argv = NULL;
//
// 	argv = malloc(argc * sizeof(char *));
// 	if (unlikely(argv == NULL)) {
// 		fprintf(stderr, "malloc() error: %s\n", strerror(errno));
// 		return NULL;
// 	}
//
//	return argv;
// }
//
// static void argv_destroy(void *argv)
// {
// 	free(argv);
// }
import "C"

import (
	"errors"
	"os"
	"unsafe"
)

func Init(serverPort uint32) error {
	argc := C.int(len(os.Args))
	argv := (*[0xff]*C.char)(C.argv_create(argc))
	defer C.argv_destroy(unsafe.Pointer(argv))

	for i := 0; i < int(argc); i++ {
		argv[i] = C.CString(os.Args[i])
		defer C.free(unsafe.Pointer(argv[i]))
	}

	ret := C.gateway_init(argc, (**C.char)(unsafe.Pointer(argv)),
	                      C.uint32_t(serverPort))
	if (ret == -1) {
		return errors.New("gateway_init() error")
	}

	return nil
}

func Exit() error {
	ret := C.gateway_exit()
	if (ret == -1) {
		return errors.New("gateway_exit() error")
	}

	return nil
}
