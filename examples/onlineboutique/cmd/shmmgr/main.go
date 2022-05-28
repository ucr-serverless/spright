// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package main

import (
	"unsafe"

	"github.com/ShixiongQi/spright/pkg/shmmgr"

	"onlineboutique/internal/common"
)

func main() {
	var err error

	err = shmmgr.Init(common.TotalNodes,
	                  unsafe.Sizeof(common.Transaction{}))
	if err != nil {
		panic(err)
	}

	select{}

	err = shmmgr.Exit()
	if err != nil {
		panic(err)
	}
}
