// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package main

// #cgo pkg-config: libdpdk
// #cgo CFLAGS: -I${SRCDIR}/../../src/include
// #cgo LDFLAGS: -L${SRCDIR}/../../src
// #cgo rte_ring LDFLAGS: -l:io_rte_ring.o
// #cgo sk_msg LDFLAGS: -l:io_sk_msg.o -lbpf
//
// #include <errno.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
//
// #include <rte_branch_prediction.h>
// #include <rte_eal.h>
// #include <rte_errno.h>
// #include <rte_memzone.h>
//
// #include "http.h"
// #include "io.h"
// #include "spright.h"
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
//
// static int nf_init(int argc, char **argv)
// {
// 	const struct rte_memzone *memzone = NULL;
// 	int ret;
//
// 	ret = rte_eal_init(argc, argv);
// 	if (unlikely(ret == -1)) {
// 		fprintf(stderr, "rte_eal_init() error: %s\n",
// 		        rte_strerror(rte_errno));
// 		goto error_0;
// 	}
//
// 	argc -= ret;
// 	argv += ret;
//
// 	if (unlikely(argc == 1)) {
// 		fprintf(stderr, "Network Function ID not provided\n");
// 		goto error_1;
// 	}
//
// 	errno = 0;
// 	node_id = strtol(argv[1], NULL, 10);
// 	if (unlikely(errno != 0 || node_id < 1)) {
// 		fprintf(stderr, "Invalid value for Network Function ID\n");
// 		goto error_1;
// 	}
//
// 	memzone = rte_memzone_lookup(MEMZONE_NAME);
// 	if (unlikely(memzone == NULL)) {
// 		fprintf(stderr, "rte_memzone_lookup() error\n");
// 		goto error_1;
// 	}
//
// 	cfg = memzone->addr;
//
// 	ret = io_init();
// 	if (unlikely(ret == -1)) {
// 		fprintf(stderr, "io_init() error\n");
// 		goto error_1;
// 	}
//
// 	return 0;
//
// error_1:
// 	rte_eal_cleanup();
// error_0:
// 	return -1;
// }
//
// static int nf_exit(void)
// {
// 	int ret;
//
// 	ret = io_exit();
// 	if (unlikely(ret == -1)) {
// 		fprintf(stderr, "io_exit() error\n");
// 		return -1;
// 	}
//
// 	ret = rte_eal_cleanup();
// 	if (unlikely(ret < 0)) {
// 		fprintf(stderr, "rte_eal_cleanup() error: %s\n",
// 		        rte_strerror(-ret));
// 		return -1;
// 	}
//
// 	return 0;
// }
//
// static int nf_io_rx(struct http_transaction **txn)
// {
// 	return io_rx((void **)txn);
// }
//
// static int nf_io_tx(struct http_transaction *txn, uint8_t next_nf)
// {
// 	return io_tx(txn, next_nf);
// }
//
// static struct http_transaction *txn_create(void)
// {
// 	struct http_transaction *txn;
// 	int ret;
//
// 	ret = rte_mempool_get(cfg->mempool, (void **)&txn);
// 	if (unlikely(ret < 0)) {
// 		fprintf(stderr, "rte_mempool_get() error: %s\n",
// 		        rte_strerror(-ret));
// 		return NULL;
// 	}
//
// 	return txn;
// }
//
// static void txn_delete(struct http_transaction *txn)
// {
// 	rte_mempool_put(cfg->mempool, txn);
// }
//
// static uint8_t route(struct http_transaction *txn)
// {
// 	uint8_t next_nf;
//
// 	txn->hop_count++;
//
// 	if (likely(txn->hop_count < cfg->route[txn->route_id].length)) {
// 		next_nf = cfg->route[txn->route_id].node[txn->hop_count];
// 	} else {
// 		next_nf = 0;
// 	}
// 	return next_nf;
// }
//
// static int get_num_workers(uint8_t nf_id)
// {
// 	uint8_t num_workers = cfg->nf[nf_id - 1].n_threads;
// 	return (int) num_workers;
// }
// static uint8_t get_route_len(uint8_t route_id)
// {
// 	return cfg->route[route_id].length;
// }
// static uint8_t get_route_hop(uint8_t route_id, uint8_t hop_idx)
// {
// 	return cfg->route[route_id].node[hop_idx];
// }
// static char* get_nf_name(uint8_t nf_id)
// {
// 	return cfg->nf[nf_id - 1].name;;
// }
// static uint8_t get_n_nfs()
// {
// 	return cfg->n_nfs;
// }
import "C"

import (
	"errors"
	"os"
	"unsafe"
	"fmt"
	"strconv"
	"time"
	"math/rand"

	"github.com/sirupsen/logrus"
	pb "github.com/GoogleCloudPlatform/microservices-demo/src/shippingservice/genproto"
)

var (
	// Route []uint8
	// RouteID uint8 = 1
	nfID uint8
	numWorkers int
	nfName string = "RecommendationService"
	nfNameToIdMap map[string]uint8
	RecommendationService = &server{}
)

var log *logrus.Logger

type ReceiveChannel struct {
    Transaction *C.struct_http_transaction
}

type TransmitChannel struct {
    Transaction *C.struct_http_transaction
	NextNF C.uint8_t
}

func init() {
	log = logrus.New()
	log.Level = logrus.DebugLevel
	log.Formatter = &logrus.JSONFormatter{
		FieldMap: logrus.FieldMap{
			logrus.FieldKeyTime:  "timestamp",
			logrus.FieldKeyLevel: "severity",
			logrus.FieldKeyMsg:   "message",
		},
		TimestampFormat: time.RFC3339Nano,
	}
	log.Out = os.Stdout
}

func nfInit() error {
	argc := C.int(len(os.Args))
	argv := (*[0xff]*C.char)(C.argv_create(argc))
	defer C.argv_destroy(unsafe.Pointer(argv))

	for i := 0; i < int(argc); i++ {
		argv[i] = C.CString(os.Args[i])
		defer C.free(unsafe.Pointer(argv[i]))
	}

	ret := C.nf_init(argc, (**C.char)(unsafe.Pointer(argv)))
	if (ret == -1) {
		return errors.New("nf_init() error")
	}

	nfID_int, _ := strconv.Atoi(os.Args[8])
	nfID = uint8(nfID_int)
	
	numWorkers = int(C.get_num_workers(C.uchar(nfID)))
	
	// RouteLen := C.get_route_len(C.uchar(RouteID))
	
	// for idx := 0; idx < int(RouteLen); idx++ {
	// 	r := C.get_route_hop(C.uchar(RouteID), C.uchar(idx))
	// 	Route = append(Route, uint8(r))
	// }
	
	// Initialize the NF Name to NF ID map
	numNF := C.get_n_nfs()
	nfNameToIdMap = make(map[string]uint8)
	for idx := 1; idx <= int(numNF); idx++ {
		// C.Gostring() seems to copy the entire length of the buffer
		nfNameToIdMap[C.GoString(C.get_nf_name(C.uchar(uint8(idx))))] = uint8(idx)
	}
	fmt.Printf("nfNameToIdMap: %v\n", nfNameToIdMap)

	if nfName != C.GoString(C.get_nf_name(C.uchar(nfID))) {
		log.Error("!!Function name does not match with the config!!")
	}
	// log.Infof("[%v (ID: %v)] Route %v has %v Hops: %v", nfName, nfID, RouteID, RouteLen, Route)
	// log.Infof("Use http://<IP_Address>:<Port>/%v/ for testing", RouteID)
	return nil
}

func nfExit() error {
	ret := C.nf_exit()
	if (ret == -1) {
		return errors.New("nf_exit() error")
	}

	return nil
}

func ioRx(rxChan chan<- ReceiveChannel) {
	log.Infof("Receiver Thread started")
	for {
		var txn = (*C.struct_http_transaction)(C.NULL)

		ret := C.nf_io_rx(&txn)
		if (ret == -1) {
			panic(errors.New("nf_io_rx() error"))
		}
	
		rxChan <- ReceiveChannel{Transaction: txn}
	}
}

func ioTx(txChan <-chan TransmitChannel) {
	log.Infof("Transmiter Thread started")
	for t := range txChan {
		ret := C.nf_io_tx(t.Transaction, t.NextNF)
		if (ret == -1) {
			panic(errors.New("nf_io_tx() error"))
		}
	}
}

func txnCreate() *C.struct_http_transaction {
	return C.txn_create()
}

func txnDelete(txn *C.struct_http_transaction) {
	C.txn_delete(txn)
}

func nfWorker(threadID int, rxChan <-chan ReceiveChannel, txChan chan<- TransmitChannel) {
	fmt.Printf("Worker Thread %v started\n", threadID)

	for rx := range rxChan {
		// fmt.Printf("Thread %v: Received msg\n", threadID)
		time.Sleep(1 * time.Second)

		txn := rx.Transaction
		var next_nf C.uint8_t
		txn.hop_count = txn.hop_count + C.uchar(1)

		next_nf = nfDispatcher(txn) // run dispatcher to select the handler

		// fmt.Printf("Next NF: %v, Current Hop: %v\n", next_nf, txn.hop_count)
		txChan <- TransmitChannel{Transaction: txn, NextNF: next_nf}
	}
}

// txHandler sets up the current NF as the caller and
// writes the name of remote handler to called in the next function
func txHandler(next_rpcHandler string, txn *C.struct_http_transaction) {
	callerNF := C.CString(nfName) // There is one copy
	defer C.free(unsafe.Pointer(callerNF))
	C.strcpy(&txn.caller_nf[0], callerNF) // There is another one copy

	cs := C.CString(next_rpcHandler) // There is one copy
	defer C.free(unsafe.Pointer(cs))
	C.strcpy(&txn.rpc_handler[0], cs) // There is another one copy
}

func nfDispatcher(txn *C.struct_http_transaction) C.uint8_t {
	var next_nf C.uint8_t
	var next_rpcHandler string

	rpcHandler := C.GoString(&txn.rpc_handler[0])
	fmt.Printf("Handler %v() in %v gets called\n", rpcHandler, nfName)

	if rpcHandler == "ListRecommendationsHandler" {
		next_nf, next_rpcHandler = ListRecommendationsHandler(txn)
	} else if rpcHandler == "ListProductsResponseHandler" {
		next_nf, next_rpcHandler = ListProductsResponseHandler(txn)
	} else {
		log.Errorf("%v is not supported by %v!", rpcHandler, nfName)
	}

	txHandler(next_rpcHandler, txn)
	fmt.Printf("%v will call %v() in %v\n", nfName, next_rpcHandler, next_nf)

	return next_nf
}

func ListRecommendationsHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	// fetch list of products from ProductCatalogService
	if val, ok := nfNameToIdMap["ProductCatalogService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "ListProductsHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to FrontendService")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

// Receive callback from ProductCatalogService
func ListProductsResponseHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	/*
	* call ListRecommendations(in1 *pb.ListRecommendationsRequest, in2 *pb.ListProductsResponse)
	*/

	// Send back to frontend
	if val, ok := nfNameToIdMap["FrontendService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "ListRecommendationsResponseHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to FrontendService")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func nf() error {
	RxChan := make(chan ReceiveChannel)
	TxChan := make(chan TransmitChannel)

	log.Infof("%v (ID: %v) is creating %v worker threads...", nfName, nfID, numWorkers)
	for idx := 1; idx <= numWorkers; idx++ {
		go nfWorker(idx, RxChan, TxChan)
	}
	
	go ioRx(RxChan)
	
	ioTx(TxChan)

	close(RxChan)
	close(TxChan)

	return nil
}

func main() {
	var err error

	err = nfInit()
	if err != nil {
		panic(err)
	}

	err = nf()
	if err != nil {
		panic(err)
	}

	err = nfExit()
	if err != nil {
		panic(err)
	}
}

// server controls Handlers.
type server struct{}

func arrayUnique(arr []string) []string {
	result := make([]string, 0, len(arr))
	temp := map[string]struct{}{}
	for i := 0; i < len(arr); i++ {
		if _, ok := temp[arr[i]]; ok != true {
			temp[arr[i]] = struct{}{}
			result = append(result, arr[i])
		}
	}
	return result
}

func DiffArray(a []string, b []string) []string {
	var diffArray []string
	temp := map[string]struct{}{}

	for _, val := range b {
		if _, ok := temp[val]; !ok {
			temp[val] = struct{}{}
		}
	}

	for _, val := range a {
		if _, ok := temp[val]; !ok {
			diffArray = append(diffArray, val)
		}
	}

	return diffArray
}

// ListRecommendations fetch list of products from product catalog stub
func (s *server) ListRecommendations(in1 *pb.ListRecommendationsRequest, in2 *pb.ListProductsResponse) (*pb.ListRecommendationsResponse, error) {
	log.Info("[ListRecommendations] received request")
	defer log.Info("[ListRecommendations] completed request")

	max_responses := 5

	/* RecommendationService calls ProductCatalogService
	dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	if s.productCatalogSvcUrl != "" {
		dopts = append(dopts, grpc.WithAuthority(s.productCatalogSvcUrl))
	}
	conn, err := grpc.DialContext(ctx, s.productCatalogSvcAddr, dopts...)
	if err != nil {
		return nil, fmt.Errorf("could not connect product catalog service: %+v", err)
	}
	defer conn.Close()
	
	cl := pb.NewProductCatalogServiceClient(conn)
	res, err := cl.ListProducts(ctx, &pb.Empty{})
	*/

	// 1. Filter products
	req_product_ids := arrayUnique(in1.GetProductIds())
	// product_list := res.GetProducts()
	product_list := in2.GetProducts()
	product_ids := make([]string, len(product_list), len(product_list))
	for i, prod := range product_list {
		product_ids[i] = prod.GetId()
	}
	product_ids = arrayUnique(product_ids)

	filtered_products := DiffArray(product_ids, req_product_ids)
	num_products := len(filtered_products)

	var num_return int
	if max_responses > num_products {
		num_return = num_products
	} else {
		num_return = max_responses
	}

	// 2. sample list of indicies to return
	indices := make([]int, num_return, num_return)
	for i, _ := range indices {
		indices[i] = rand.Intn(num_products)
	}

	prod_list := make([]string, num_return, num_return)
	for i, _ := range prod_list {
		prod_list[i] = filtered_products[indices[i]]
	}

	// 3. Generate a response.
	return &pb.ListRecommendationsResponse{
		ProductIds: prod_list,
	}, nil

}