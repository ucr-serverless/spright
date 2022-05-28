// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package main

import "github.com/ShixiongQi/spright/pkg/gateway"

func main() {
	var err error

	err = gateway.Init(8080)
	if err != nil {
		panic(err)
	}

	select{}

	err = gateway.Exit()
	if err != nil {
		panic(err)
	}
}
