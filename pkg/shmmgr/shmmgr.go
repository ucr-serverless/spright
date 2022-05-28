// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package shmmgr

// #cgo pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}/../../include
// #cgo LDFLAGS: -L${SRCDIR}/../../lib
// #cgo rte_ring LDFLAGS: -l:shmmgr_rte_ring.a
// #cgo sk_msg LDFLAGS: -l:shmmgr_sk_msg.a -lbpf
//
// #include <errno.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
//
// #include <rte_branch_prediction.h>
//
// #include <shmmgr.h>
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

func Init(TotalNodes uint8, appTransactionSize uintptr) error {
	argc := C.int(len(os.Args))
	argv := (*[0xff]*C.char)(C.argv_create(argc))
	defer C.argv_destroy(unsafe.Pointer(argv))

	for i := 0; i < int(argc); i++ {
		argv[i] = C.CString(os.Args[i])
		defer C.free(unsafe.Pointer(argv[i]))
	}

	ret := C.shmmgr_init(argc, (**C.char)(unsafe.Pointer(argv)),
	                     C.uint8_t(TotalNodes),
	                     C.unsigned(appTransactionSize))
	if (ret == -1) {
		return errors.New("shmmgr_init() error")
	}

	return nil
}

func Exit() error {
	ret := C.shmmgr_exit()
	if (ret == -1) {
		return errors.New("shmmgr_exit() error")
	}

	return nil
}
