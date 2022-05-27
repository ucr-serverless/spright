// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package shmmgr

// #cgo pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}
//
// #include "shmmgr.c"
import "C"

import (
	"spright/internal/io"
)

func Init() error {
}

func Exit() error {
}
