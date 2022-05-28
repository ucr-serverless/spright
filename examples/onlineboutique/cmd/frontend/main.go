// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package main

import (
	"unsafe"

	"github.com/ShixiongQi/spright/pkg/io"
	"github.com/ShixiongQi/spright/pkg/node"

	"onlineboutique/internal/common"
)

func frontend() {
	var txnHttp io.HttpTransaction
	var txnApp *common.Transaction
	var _txnApp *byte

	nodeIDSrc, txn, err := io.Rx()
	if err != nil {
		panic(err)
	}

	if nodeIDSrc == 0 {
		io.GetHttpTransaction(txn, &txnHttp)

		print(txnHttp.Request)
	} else {
		_txnApp = io.GetAppTransaction(txn)
		txnApp = (*common.Transaction)(unsafe.Pointer(_txnApp))

		txnApp.ID = 1234
	}

	err = io.Tx(txn, common.NodeIDGateway)
	if err != nil {
		panic(err)
	}
}

func main() {
	var err error

	err = node.Init(common.NodeIDFrontend)
	if err != nil {
		panic(err)
	}

	frontend()

	select{}

	err = node.Exit()
	if err != nil {
		panic(err)
	}
}
