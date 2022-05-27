// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package gateway

// #cgo pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}
//
// #include "gateway.c"
import "C"

import (
	"spright/internal/io"
)

func Init() error {
}

func Exit() error {
}
