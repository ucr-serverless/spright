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
	"github.com/google/uuid"
	pb "github.com/GoogleCloudPlatform/microservices-demo/src/shippingservice/genproto"
	money "github.com/GoogleCloudPlatform/microservices-demo/src/checkoutservice/money"
)

const (
	usdCurrency = "USD"
)

var (
	// Route []uint8
	// RouteID uint8 = 1
	nfID uint8
	numWorkers int
	nfName string = "CheckoutService"
	nfNameToIdMap map[string]uint8
	CheckoutService = &server{}
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
		// time.Sleep(1 * time.Second)

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
	callerNF := C.CString(nfName)
	defer C.free(unsafe.Pointer(callerNF))
	C.strcpy(&txn.caller_nf[0], callerNF)

	cs := C.CString(next_rpcHandler)
	defer C.free(unsafe.Pointer(cs))
	C.strcpy(&txn.rpc_handler[0], cs)
}

func nfDispatcher(txn *C.struct_http_transaction) C.uint8_t {
	var next_nf C.uint8_t
	var next_rpcHandler string

	rpcHandler := C.GoString(&txn.rpc_handler[0])
	// fmt.Printf("Handler %v() in %v gets called\n", rpcHandler, nfName)

	if rpcHandler == "PlaceOrderHandler" {
		next_nf, next_rpcHandler = PlaceOrderHandler(txn)
	} else if rpcHandler == "prepareOrderItemsAndShippingQuoteFromCartHandler" {
		next_nf, next_rpcHandler = prepareOrderItemsAndShippingQuoteFromCartHandler(txn)
	} else {
		log.Errorf("%v is not supported by %v!", rpcHandler, nfName)
	}

	txHandler(next_rpcHandler, txn)
	fmt.Printf("%v will call %v() in %v\n", nfName, next_rpcHandler, next_nf)

	return next_nf
}

func PlaceOrderHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap[C.GoString(&txn.caller_nf[0])]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "PlaceOrderResponseHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func prepareOrderItemsAndShippingQuoteFromCartHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap[C.GoString(&txn.caller_nf[0])]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "prepareOrderItemsAndShippingQuoteFromCartResponseHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func quoteShippingHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["ShippingService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "GetQuoteHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func shipOrderHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["ShippingService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "ShipOrderHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func sendOrderConfirmationHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["EmailService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "SendOrderConfirmationHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func chargeCardHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["PaymentService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "ChargeHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func convertCurrencyHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["CurrencyService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "ConvertHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func emptyUserCartHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["CartService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "EmptyCartHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func getUserCartHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["CartService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "GetCartHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
		next_nf = 0
		next_rpcHandler = "ErrorResponseHandler"
	}

	return next_nf, next_rpcHandler
}

func getUserCartHandler(txn *C.struct_http_transaction) (C.uint8_t, string) {
	var next_nf C.uint8_t
	var next_rpcHandler string

	if val, ok := nfNameToIdMap["CartService"]; ok {
		next_nf = C.uchar(val)
		next_rpcHandler = "AddItemHandler"
	} else {
		// TODO: add error codes support in the txn structure
		log.Error("Unknown service! Report error to frontend")
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

func (cs *checkoutService) PlaceOrder(req *pb.PlaceOrderRequest) (*pb.PlaceOrderResponse, error) {
	log.Infof("[PlaceOrder] user_id=%q user_currency=%q", req.UserId, req.UserCurrency)

	orderID, err := uuid.NewUUID()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to generate order uuid")
	}

	prep, err := cs.prepareOrderItemsAndShippingQuoteFromCart(req.UserId, req.UserCurrency, req.Address)
	if err != nil {
		return nil, status.Errorf(codes.Internal, err.Error())
	}

	total := pb.Money{CurrencyCode: req.UserCurrency,
		Units: 0,
		Nanos: 0}
	total = money.Must(money.Sum(total, *prep.shippingCostLocalized))
	for _, it := range prep.orderItems {
		multPrice := money.MultiplySlow(*it.Cost, uint32(it.GetItem().GetQuantity()))
		total = money.Must(money.Sum(total, multPrice))
	}

	txID, err := cs.chargeCard(&total, req.CreditCard)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to charge card: %+v", err)
	}
	log.Infof("payment went through (transaction_id: %s)", txID)

	shippingTrackingID, err := cs.shipOrder(req.Address, prep.cartItems)
	if err != nil {
		return nil, status.Errorf(codes.Unavailable, "shipping error: %+v", err)
	}

	_ = cs.emptyUserCart(req.UserId)

	orderResult := &pb.OrderResult{
		OrderId:            orderID.String(),
		ShippingTrackingId: shippingTrackingID,
		ShippingCost:       prep.shippingCostLocalized,
		ShippingAddress:    req.Address,
		Items:              prep.orderItems,
	}

	if err := cs.sendOrderConfirmation(req.Email, orderResult); err != nil {
		log.Warnf("failed to send order confirmation to %q: %+v", req.Email, err)
	} else {
		log.Infof("order confirmation email sent to %q", req.Email)
	}
	resp := &pb.PlaceOrderResponse{Order: orderResult}
	return resp, nil
}

type orderPrep struct {
	orderItems            []*pb.OrderItem
	cartItems             []*pb.CartItem
	shippingCostLocalized *pb.Money
}

func (cs *checkoutService) prepareOrderItemsAndShippingQuoteFromCart(userID, userCurrency string, address *pb.Address) (orderPrep, error) {
	var out orderPrep
	cartItems, err := cs.getUserCart(userID)
	if err != nil {
		return out, fmt.Errorf("cart failure: %+v", err)
	}
	orderItems, err := cs.prepOrderItems(cartItems, userCurrency)
	if err != nil {
		return out, fmt.Errorf("failed to prepare order: %+v", err)
	}
	shippingUSD, err := cs.quoteShipping(address, cartItems)
	if err != nil {
		return out, fmt.Errorf("shipping quote failure: %+v", err)
	}
	shippingPrice, err := cs.convertCurrency(shippingUSD, userCurrency)
	if err != nil {
		return out, fmt.Errorf("failed to convert shipping cost to currency: %+v", err)
	}

	out.shippingCostLocalized = shippingPrice
	out.cartItems = cartItems
	out.orderItems = orderItems
	return out, nil
}

func (cs *checkoutService) quoteShipping(address *pb.Address, items []*pb.CartItem) (*pb.Money, error) {
	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.shippingSvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.shippingSvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.shippingSvcAddr, dopts...)
	// if err != nil {
	// 	return nil, fmt.Errorf("could not connect shipping service: %+v", err)
	// }
	// defer conn.Close()

	shippingQuote, err := pb.NewShippingServiceClient(conn).
		GetQuote(&pb.GetQuoteRequest{
			Address: address,
			Items:   items})
	if err != nil {
		return nil, fmt.Errorf("failed to get shipping quote: %+v", err)
	}
	return shippingQuote.GetCostUsd(), nil
}

func (cs *checkoutService) getUserCart(userID string) ([]*pb.CartItem, error) {
	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.cartSvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.cartSvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.cartSvcAddr, dopts...)
	// if err != nil {
	// 	return nil, fmt.Errorf("could not connect cart service: %+v", err)
	// }
	// defer conn.Close()

	cart, err := pb.NewCartServiceClient(conn).GetCart(&pb.GetCartRequest{UserId: userID})
	if err != nil {
		return nil, fmt.Errorf("failed to get user cart during checkout: %+v", err)
	}
	return cart.GetItems(), nil
}

func (cs *checkoutService) emptyUserCart(userID string) error {
	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.cartSvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.cartSvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.cartSvcAddr, dopts...)
	// if err != nil {
	// 	return fmt.Errorf("could not connect cart service: %+v", err)
	// }
	// defer conn.Close()

	if _, err = pb.NewCartServiceClient(conn).EmptyCart(&pb.EmptyCartRequest{UserId: userID}); err != nil {
		return fmt.Errorf("failed to empty user cart during checkout: %+v", err)
	}
	return nil
}

func (cs *checkoutService) prepOrderItems(items []*pb.CartItem, userCurrency string) ([]*pb.OrderItem, error) {
	out := make([]*pb.OrderItem, len(items))

	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.productCatalogSvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.productCatalogSvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.productCatalogSvcAddr, dopts...)
	// if err != nil {
	// 	return nil, fmt.Errorf("could not connect product catalog service: %+v", err)
	// }
	// defer conn.Close()
	// cl := pb.NewProductCatalogServiceClient(conn)

	for i, item := range items {
		product, err := cl.GetProduct(&pb.GetProductRequest{Id: item.GetProductId()})
		if err != nil {
			return nil, fmt.Errorf("failed to get product #%q", item.GetProductId())
		}
		price, err := cs.convertCurrency(product.GetPriceUsd(), userCurrency)
		if err != nil {
			return nil, fmt.Errorf("failed to convert price of %q to %s", item.GetProductId(), userCurrency)
		}
		out[i] = &pb.OrderItem{
			Item: item,
			Cost: price}
	}
	return out, nil
}

func (cs *checkoutService) convertCurrency(from *pb.Money, toCurrency string) (*pb.Money, error) {
	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.currencySvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.currencySvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.currencySvcAddr, dopts...)
	// if err != nil {
	// 	return nil, fmt.Errorf("could not connect currency service: %+v", err)
	// }
	// defer conn.Close()
	result, err := pb.NewCurrencyServiceClient(conn).Convert(context.TODO(), &pb.CurrencyConversionRequest{
		From:   from,
		ToCode: toCurrency})
	if err != nil {
		return nil, fmt.Errorf("failed to convert currency: %+v", err)
	}
	return result, err
}

func (cs *checkoutService) chargeCard(amount *pb.Money, paymentInfo *pb.CreditCardInfo) (string, error) {
	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.paymentSvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.paymentSvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.paymentSvcAddr, dopts...)
	// if err != nil {
	// 	return "", fmt.Errorf("failed to connect payment service: %+v", err)
	// }
	// defer conn.Close()

	paymentResp, err := pb.NewPaymentServiceClient(conn).Charge(&pb.ChargeRequest{
		Amount:     amount,
		CreditCard: paymentInfo})
	if err != nil {
		return "", fmt.Errorf("could not charge the card: %+v", err)
	}
	return paymentResp.GetTransactionId(), nil
}

func (cs *checkoutService) sendOrderConfirmation(email string, order *pb.OrderResult) error {
	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.emailSvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.emailSvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.emailSvcAddr, dopts...)
	// if err != nil {
	// 	return fmt.Errorf("failed to connect email service: %+v", err)
	// }
	// defer conn.Close()
	_, err = pb.NewEmailServiceClient(conn).SendOrderConfirmation(&pb.SendOrderConfirmationRequest{
		Email: email,
		Order: order})
	return err
}

func (cs *checkoutService) shipOrder(address *pb.Address, items []*pb.CartItem) (string, error) {
	// dopts := []grpc.DialOption{grpc.WithInsecure(), grpc.WithStatsHandler(&ocgrpc.ClientHandler{})}
	// if cs.shippingSvcUrl != "" {
	// 	dopts = append(dopts, grpc.WithAuthority(cs.shippingSvcUrl))
	// }
	// conn, err := grpc.DialContext(ctx, cs.shippingSvcAddr, dopts...)
	// if err != nil {
	// 	return "", fmt.Errorf("failed to connect email service: %+v", err)
	// }
	// defer conn.Close()
	resp, err := pb.NewShippingServiceClient(conn).ShipOrder(&pb.ShipOrderRequest{
		Address: address,
		Items:   items})
	if err != nil {
		return "", fmt.Errorf("shipment failed: %+v", err)
	}
	return resp.GetTrackingId(), nil
}
