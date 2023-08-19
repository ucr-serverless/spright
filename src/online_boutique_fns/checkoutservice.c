// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <uuid/uuid.h>

#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_memzone.h>

#include "http.h"
#include "io.h"
#include "spright.h"
#include "utility.h"

static int pipefd_rx[UINT8_MAX][2];
static int pipefd_tx[UINT8_MAX][2];

char defaultCurrency[5] = "CAD";

void prepareOrderItemsAndShippingQuoteFromCart(struct http_transaction *txn);

static void sendOrderConfirmation(struct http_transaction *txn) {
	uuid_t binuuid; uuid_generate_random(binuuid);
	uuid_unparse(binuuid, txn->order_result.OrderId);
	strcpy(txn->order_result.ShippingTrackingId, txn->ship_order_response.TrackingId);
	txn->order_result.ShippingCost = txn->get_quote_response.CostUsd;
	txn->order_result.ShippingAddress = txn->place_order_request.address;

	strcpy(txn->email_req.Email, txn->place_order_request.Email);

	strcpy(txn->rpc_handler, "SendOrderConfirmation");
	txn->caller_fn = CHECKOUT_SVC;
	txn->next_fn = EMAIL_SVC;
	txn->checkoutsvc_hop_cnt++;
}

static void emptyUserCart(struct http_transaction *txn) {
	EmptyCartRequest *in = &txn->empty_cart_request;
	strcpy(in->UserId, "ucr-students");

	strcpy(txn->rpc_handler, "EmptyCart");
	txn->caller_fn = CHECKOUT_SVC;
	txn->next_fn = CART_SVC;
	txn->checkoutsvc_hop_cnt++;
}

static void shipOrder(struct http_transaction *txn) {
	ShipOrderRequest *in = &txn->ship_order_request;
	strcpy(in->address.StreetAddress, txn->place_order_request.address.StreetAddress);
	strcpy(in->address.City, txn->place_order_request.address.City);
	strcpy(in->address.State, txn->place_order_request.address.State);
	strcpy(in->address.Country, txn->place_order_request.address.Country);
	in->address.ZipCode = txn->place_order_request.address.ZipCode;

	strcpy(txn->rpc_handler, "ShipOrder");
	txn->caller_fn = CHECKOUT_SVC;
	txn->next_fn = SHIPPING_SVC;
	txn->checkoutsvc_hop_cnt++;
}

static void chargeCard(struct http_transaction *txn) {
	strcpy(txn->charge_request.CreditCard.CreditCardNumber, txn->place_order_request.CreditCard.CreditCardNumber);
	txn->charge_request.CreditCard.CreditCardCvv = txn->place_order_request.CreditCard.CreditCardCvv;
	txn->charge_request.CreditCard.CreditCardExpirationYear = txn->place_order_request.CreditCard.CreditCardExpirationYear;
	txn->charge_request.CreditCard.CreditCardExpirationMonth = txn->place_order_request.CreditCard.CreditCardExpirationMonth;

	strcpy(txn->charge_request.Amount.CurrencyCode, txn->total_price.CurrencyCode);
	txn->charge_request.Amount.Units = txn->total_price.Units;
	txn->charge_request.Amount.Nanos = txn->total_price.Nanos;

	strcpy(txn->rpc_handler, "Charge");
	txn->caller_fn = CHECKOUT_SVC;
	txn->next_fn = PAYMENT_SVC;
	txn->checkoutsvc_hop_cnt++;
}

static void calculateTotalPrice(struct http_transaction *txn) {
	printf("[%s()] Calculating total price...\n", __func__);
	int i = 0;
	for (i = 0; i < txn->orderItemViewCntr; i++) {
		MultiplySlow(&txn->order_item_view[i].Cost, txn->order_item_view[i].Item.Quantity);
		Sum(&txn->total_price, &txn->order_item_view[i].Cost);
	}
	printf("[%s()]\t\t>>>>>> priceItem(s) Subtotal: %ld.%d\n", __func__, txn->total_price.Units, txn->total_price.Nanos);
	printf("[%s()]\t\t>>>>>> Shipping & Handling: %ld.%d\n", __func__, txn->get_quote_response.CostUsd.Units, txn->get_quote_response.CostUsd.Nanos);
	Sum(&txn->total_price, &txn->get_quote_response.CostUsd);
	
	return;
}

static void returnResponseToFrontendWithOrderResult(struct http_transaction *txn) {
	txn->next_fn = FRONTEND;
	txn->caller_fn = CHECKOUT_SVC;
}

static void returnResponseToFrontend(struct http_transaction *txn) {
	txn->next_fn = FRONTEND;
	txn->caller_fn = CHECKOUT_SVC;
}

// Convert currency for a ShippingQuote
static void convertCurrencyOfShippingQuote(struct http_transaction *txn) {
	if (strcmp(defaultCurrency, "USD") == 0) {
		printf("[%s()] Default Currency is USD. Skip convertCurrencyOfShippingQuote\n", __func__);
		txn->get_quote_response.conversion_flag = true;
		txn->checkoutsvc_hop_cnt++;
		prepareOrderItemsAndShippingQuoteFromCart(txn);
	} else {
		if (txn->get_quote_response.conversion_flag == true) {
			txn->get_quote_response.CostUsd = txn->currency_conversion_result;
			printf("[%s()] Write back convertCurrencyOfShippingQuote\n", __func__);
			txn->checkoutsvc_hop_cnt++;
			prepareOrderItemsAndShippingQuoteFromCart(txn);

		} else {
			printf("[%s()] Default Currency is %s. Do convertCurrencyOfShippingQuote\n", __func__, defaultCurrency);
			strcpy(txn->currency_conversion_req.ToCode, defaultCurrency);
			strcpy(txn->currency_conversion_req.From.CurrencyCode, txn->get_quote_response.CostUsd.CurrencyCode);
			txn->currency_conversion_req.From.Units = txn->get_quote_response.CostUsd.Units;
			txn->currency_conversion_req.From.Nanos = txn->get_quote_response.CostUsd.Nanos;

			strcpy(txn->rpc_handler, "Convert");
			txn->caller_fn = CHECKOUT_SVC;
			txn->next_fn = CURRENCY_SVC;

			txn->get_quote_response.conversion_flag = true;
		}
	}
	return;
}

static void quoteShipping(struct http_transaction *txn) {
	GetQuoteRequest* in = &txn->get_quote_request;
	in->num_items = 0;
	txn->get_quote_response.conversion_flag = false;

	int i;
	for (i = 0; i < txn->get_cart_response.num_items; i++) {
		in->Items[i].Quantity = i + 1;
		in->num_items++;
	}

	strcpy(txn->rpc_handler, "GetQuote");
	txn->caller_fn = CHECKOUT_SVC;
	txn->next_fn = SHIPPING_SVC;
	txn->checkoutsvc_hop_cnt++;
}


// Convert currency for products in the cart
static void convertCurrencyOfCart(struct http_transaction *txn) {
	if (strcmp(defaultCurrency, "USD") == 0) {
		printf("Default Currency is USD. Skip convertCurrencyOfCart\n");
		txn->checkoutsvc_hop_cnt++;
		prepareOrderItemsAndShippingQuoteFromCart(txn); return;
	} else {
		if (txn->orderItemCurConvertCntr != 0) {
			txn->order_item_view[txn->orderItemCurConvertCntr - 1].Cost = txn->currency_conversion_result;
		}

		if (txn->orderItemCurConvertCntr < txn->orderItemViewCntr) {
			printf("Default Currency is %s. Do convertCurrencyOfCart\n", defaultCurrency);
			strcpy(txn->currency_conversion_req.ToCode, defaultCurrency);
			txn->currency_conversion_req.From = txn->order_item_view[txn->orderItemCurConvertCntr].Cost;

			strcpy(txn->rpc_handler, "Convert");
			txn->caller_fn = CHECKOUT_SVC;
			txn->next_fn = CURRENCY_SVC;

			txn->orderItemCurConvertCntr++;
			return;
		} else {
			txn->checkoutsvc_hop_cnt++;
			prepareOrderItemsAndShippingQuoteFromCart(txn); return;
		}
	}
}

static void getOrderItemInfo(struct http_transaction *txn) {
	printf("[%s()] %d items in the cart.\n", __func__, txn->get_cart_response.num_items);
	if (txn->get_cart_response.num_items <= 0) {
		printf("[%s()] None items in the cart.\n", __func__);
		txn->total_price.Units = 0;
		txn->total_price.Nanos = 0;
		returnResponseToFrontend(txn); return;
	}

	if (txn->orderItemViewCntr != 0) {
		strcpy(txn->order_item_view[txn->orderItemViewCntr - 1].Item.ProductId, txn->get_product_response.Id);
		txn->order_item_view[txn->orderItemViewCntr - 1].Cost = txn->get_product_response.PriceUsd;
	}

	if (txn->orderItemViewCntr < txn->get_cart_response.num_items) {
		strcpy(txn->get_product_request.Id, txn->get_cart_response.Items[txn->orderItemViewCntr].ProductId);
		// printf("Product ID: %s\n", txn->get_product_request.Id);

		strcpy(txn->rpc_handler, "GetProduct");
		txn->caller_fn = CHECKOUT_SVC;
		txn->next_fn = PRODUCTCATA_SVC;

		txn->orderItemViewCntr++;
	} else {
		txn->orderItemCurConvertCntr = 0;
		convertCurrencyOfCart(txn);
		txn->checkoutsvc_hop_cnt++;
	}
}

static void getCart(struct http_transaction *txn) {
	strcpy(txn->get_cart_request.UserId, "ucr-students");

	strcpy(txn->rpc_handler, "GetCart");
	txn->caller_fn = CHECKOUT_SVC;
	txn->next_fn = CART_SVC;
	txn->checkoutsvc_hop_cnt++;
}

static void prepOrderItems(struct http_transaction *txn) {

	if (txn->checkoutsvc_hop_cnt == 1) {
		getOrderItemInfo(txn);
	} else if (txn->checkoutsvc_hop_cnt == 2) {
		convertCurrencyOfCart(txn);
	} else {
		printf("[%s()] prepOrderItems doesn't know what to do for HOP %u.\n", __func__, txn->checkoutsvc_hop_cnt);
		returnResponseToFrontend(txn);
	}
}

void prepareOrderItemsAndShippingQuoteFromCart(struct http_transaction *txn) {
	printf("[%s()] Call prepareOrderItemsAndShippingQuoteFromCart ### Hop: %u\n", __func__, txn->checkoutsvc_hop_cnt);

	if (txn->checkoutsvc_hop_cnt == 0) {
		getCart(txn);
		txn->orderItemViewCntr = 0;

	} else if (txn->checkoutsvc_hop_cnt >= 1 && txn->checkoutsvc_hop_cnt <= 2) {
		prepOrderItems(txn);

	} else if (txn->checkoutsvc_hop_cnt == 3) {
		quoteShipping(txn);

	} else if (txn->checkoutsvc_hop_cnt == 4) {
		convertCurrencyOfShippingQuote(txn);

	} else if (txn->checkoutsvc_hop_cnt == 5) {
		calculateTotalPrice(txn);
		chargeCard(txn);

	} else if (txn->checkoutsvc_hop_cnt == 6) {
		shipOrder(txn);

	} else if (txn->checkoutsvc_hop_cnt == 7) {
		emptyUserCart(txn);

	} else if (txn->checkoutsvc_hop_cnt == 8) {
		sendOrderConfirmation(txn);

	} else if (txn->checkoutsvc_hop_cnt == 9) {
		returnResponseToFrontendWithOrderResult(txn);

	} else {
		printf("[%s()] prepareOrderItemsAndShippingQuoteFromCart doesn't know what to do for HOP %u.\n", __func__, txn->checkoutsvc_hop_cnt);
		returnResponseToFrontend(txn);
	}
}

static void PlaceOrder(struct http_transaction *txn) {
	prepareOrderItemsAndShippingQuoteFromCart(txn);
}

static void *nf_worker(void *arg)
{
	struct http_transaction *txn = NULL;
	ssize_t bytes_written;
	ssize_t bytes_read;
	uint8_t index;

	/* TODO: Careful with this pointer as it may point to a stack */
	index = (uint64_t)arg;

	while (1) {
		bytes_read = read(pipefd_rx[index][0], &txn,
		                  sizeof(struct http_transaction *));
		if (unlikely(bytes_read == -1)) {
			fprintf(stderr, "read() error: %s\n", strerror(errno));
			return NULL;
		}

		// if (strcmp(txn->rpc_handler, "PlaceOrder") == 0) {
		PlaceOrder(txn);
		// } else {
		// 	printf("%s() is not supported\n", txn->rpc_handler);
		// 	printf("\t\t#### Run Mock Test ####\n");
		// 	// MockEmailRequest(txn);
		// 	// SendOrderConfirmation(txn);
		// }

		bytes_written = write(pipefd_tx[index][1], &txn,
		                      sizeof(struct http_transaction *));
		if (unlikely(bytes_written == -1)) {
			fprintf(stderr, "write() error: %s\n", strerror(errno));
			return NULL;
		}
	}

	return NULL;
}

static void *nf_rx(void *arg)
{
	struct http_transaction *txn = NULL;
	ssize_t bytes_written;
	uint8_t i;
	int ret;

	for (i = 0; ; i = (i + 1) % cfg->nf[node_id - 1].n_threads) {
		ret = io_rx((void **)&txn);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "io_rx() error\n");
			return NULL;
		}

		bytes_written = write(pipefd_rx[i][1], &txn,
		                      sizeof(struct http_transaction *));
		if (unlikely(bytes_written == -1)) {
			fprintf(stderr, "write() error: %s\n", strerror(errno));
			return NULL;
		}
	}

	return NULL;
}

static void *nf_tx(void *arg)
{
	struct epoll_event event[UINT8_MAX]; /* TODO: Use Macro */
	struct http_transaction *txn = NULL;
	ssize_t bytes_read;
	uint8_t i;
	int n_fds;
	int epfd;
	int ret;

	epfd = epoll_create1(0);
	if (unlikely(epfd == -1)) {
		fprintf(stderr, "epoll_create1() error: %s\n", strerror(errno));
		return NULL;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = fcntl(pipefd_tx[i][0], F_SETFL, O_NONBLOCK);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "fcntl() error: %s\n", strerror(errno));
			return NULL;
		}

		event[0].events = EPOLLIN;
		event[0].data.fd = pipefd_tx[i][0];

		ret = epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd_tx[i][0],
		                &event[0]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "epoll_ctl() error: %s\n",
			        strerror(errno));
			return NULL;
		}
	}

	while (1) {
		n_fds = epoll_wait(epfd, event, cfg->nf[node_id - 1].n_threads,
		                   -1);
		if (unlikely(n_fds == -1)) {
			fprintf(stderr, "epoll_wait() error: %s\n",
			        strerror(errno));
			return NULL;
		}

		for (i = 0; i < n_fds; i++) {
			bytes_read = read(event[i].data.fd, &txn,
			                  sizeof(struct http_transaction *));
			if (unlikely(bytes_read == -1)) {
				fprintf(stderr, "read() error: %s\n",
				        strerror(errno));
				return NULL;
			}

			ret = io_tx(txn, txn->next_fn);
			if (unlikely(ret == -1)) {
				fprintf(stderr, "io_tx() error\n");
				return NULL;
			}
		}
	}

	return NULL;
}

/* TODO: Cleanup on errors */
static int nf(uint8_t nf_id)
{
	const struct rte_memzone *memzone = NULL;
	pthread_t thread_worker[UINT8_MAX];
	pthread_t thread_rx;
	pthread_t thread_tx;
	uint8_t i;
	int ret;

	node_id = nf_id;

	memzone = rte_memzone_lookup(MEMZONE_NAME);
	if (unlikely(memzone == NULL)) {
		fprintf(stderr, "rte_memzone_lookup() error\n");
		return -1;
	}

	cfg = memzone->addr;

	ret = io_init();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_init() error\n");
		return -1;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = pipe(pipefd_rx[i]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "pipe() error: %s\n", strerror(errno));
			return -1;
		}

		ret = pipe(pipefd_tx[i]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "pipe() error: %s\n", strerror(errno));
			return -1;
		}
	}

	ret = pthread_create(&thread_rx, NULL, &nf_rx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_create() error: %s\n", strerror(ret));
		return -1;
	}

	ret = pthread_create(&thread_tx, NULL, &nf_tx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_create() error: %s\n", strerror(ret));
		return -1;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = pthread_create(&thread_worker[i], NULL, &nf_worker,
		                     (void *)(uint64_t)i);
		if (unlikely(ret != 0)) {
			fprintf(stderr, "pthread_create() error: %s\n",
			        strerror(ret));
			return -1;
		}
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = pthread_join(thread_worker[i], NULL);
		if (unlikely(ret != 0)) {
			fprintf(stderr, "pthread_join() error: %s\n",
			        strerror(ret));
			return -1;
		}
	}

	ret = pthread_join(thread_rx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_join() error: %s\n", strerror(ret));
		return -1;
	}

	ret = pthread_join(thread_tx, NULL);
	if (unlikely(ret != 0)) {
		fprintf(stderr, "pthread_join() error: %s\n", strerror(ret));
		return -1;
	}

	for (i = 0; i < cfg->nf[node_id - 1].n_threads; i++) {
		ret = close(pipefd_rx[i][0]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}

		ret = close(pipefd_rx[i][1]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}

		ret = close(pipefd_tx[i][0]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}

		ret = close(pipefd_tx[i][1]);
		if (unlikely(ret == -1)) {
			fprintf(stderr, "close() error: %s\n", strerror(errno));
			return -1;
		}
	}

	ret = io_exit();
	if (unlikely(ret == -1)) {
		fprintf(stderr, "io_exit() error\n");
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	uint8_t nf_id;
	int ret;

	ret = rte_eal_init(argc, argv);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "rte_eal_init() error: %s\n",
		        rte_strerror(rte_errno));
		goto error_0;
	}

	argc -= ret;
	argv += ret;

	if (unlikely(argc == 1)) {
		fprintf(stderr, "Network Function ID not provided\n");
		goto error_1;
	}

	errno = 0;
	nf_id = strtol(argv[1], NULL, 10);
	if (unlikely(errno != 0 || nf_id < 1)) {
		fprintf(stderr, "Invalid value for Network Function ID\n");
		goto error_1;
	}

	ret = nf(nf_id);
	if (unlikely(ret == -1)) {
		fprintf(stderr, "nf() error\n");
		goto error_1;
	}

	ret = rte_eal_cleanup();
	if (unlikely(ret < 0)) {
		fprintf(stderr, "rte_eal_cleanup() error: %s\n",
		        rte_strerror(-ret));
		goto error_0;
	}

	return 0;

error_1:
	rte_eal_cleanup();
error_0:
	return 1;
}
