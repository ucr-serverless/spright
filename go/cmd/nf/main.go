// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package main

import (
	"../../internal/spright"
)

func main() {
	var err error

	err = spright.NfInit()
	if err != nil {
		panic(err)
	}

	err = spright.NfExit()
	if err != nil {
		panic(err)
	}
}
