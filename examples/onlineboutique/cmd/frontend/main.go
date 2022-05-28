// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package main

import (
	"github.com/ShixiongQi/spright/pkg/node"

	"onlineboutique/internal/common"
)

func main() {
	var err error

	err = node.Init(common.NodeIDFrontend)
	if err != nil {
		panic(err)
	}

	select{}

	err = node.Exit()
	if err != nil {
		panic(err)
	}
}
