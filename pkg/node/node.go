// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package node

// #cgo pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}
//
// #include "node.c"
import "C"

import (
	"spright/internal/io"
)

func Init() error {
}

func Exit() error {
}
