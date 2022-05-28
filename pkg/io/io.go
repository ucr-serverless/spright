// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package io

// #cgo pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}/../../src/include
// #cgo LDFLAGS: -L${SRCDIR}/../../lib
// #cgo rte_ring LDFLAGS: -l:io_rte_ring.a
// #cgo sk_msg LDFLAGS: -l:io_sk_msg.a -lbpf
//
// #include "http.h"
// #include "io.h"
//
// int _io_rx(uint8_t **obj)
// {
//	return io_rx((void **)obj);
// }
//
// int _io_tx(uint8_t *obj, uint8_t node_id_dst)
// {
//	return io_tx(obj, node_id_dst);
// }
//
// void *get_app_transaction(struct http_transaction *txn)
// {
//	return txn + 1;
// }
import "C"

import (
	"errors"
	"unsafe"
)

type httpTransaction struct {
	lengthRequest uint32
	lengthResponse uint32

	Request *string
	Response *string
}

func Rx() (uint8, *byte, error) {
	var _txn *C.uint8_t
	var txn *byte

	ret := C._io_rx(&_txn)
	if (ret == -1) {
		return 255, nil, errors.New("io_rx() error")
	}

	txn = (*byte)(txn)

	return uint8(ret), txn, nil
}

func Tx(txn *byte, nodeIDDst uint8) error {
	var _txn *C.uint8_t

	_txn = (*C.uint8_t)(txn)

	ret := C._io_tx(_txn, C.uint8_t(nodeIDDst))
	if (ret == -1) {
		return errors.New("io_tx() error")
	}

	return nil
}

func GetHttpTransaction(txn *byte, txnHTTP *httpTransaction) {
	var _txn *C.struct_http_transaction

	_txn = (*C.struct_http_transaction)(unsafe.Pointer(txn))

	txnHTTP.lengthRequest = uint32(_txn.length_request)
	txnHTTP.lengthResponse = uint32(_txn.length_response)
	txnHTTP.Request = (*string)(unsafe.Pointer(&_txn.request))
	txnHTTP.Response = (*string)(unsafe.Pointer(&_txn.response))
}

func GetAppTransaction(txn *byte) *byte {
	var _txn *C.struct_http_transaction

	_txn = (*C.struct_http_transaction)(unsafe.Pointer(txn));

	return (*byte)(C.get_app_transaction(_txn))
}
