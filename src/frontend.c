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

#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_memzone.h>

#include "http.h"
#include "io.h"
#include "spright.h"
#include "utility.h"
#include "shm_rpc.h"

static int pipefd_rx[UINT8_MAX][2];
static int pipefd_tx[UINT8_MAX][2];

// char defaultCurrency[5] = "CAD";

static void setCurrencyHandler(struct http_transaction *txn) {
	printf("Call setCurrencyHandler\n");
	char* query = httpQueryParser(txn->request);
	char _defaultCurrency[5] = "CAD";
	strcpy(_defaultCurrency, strchr(query, '=') + 1);

	txn->hop_count += 100;
	txn->next_fn = GATEWAY; // Hack: force gateway to return a response
}

static void homeHandler(struct http_transaction *txn) {
	printf("Call homeHandler ### Hop: %u\n", txn->hop_count);

	if (txn->hop_count == 0) {
		getCurrencies(txn);

	} else if (txn->hop_count == 1) {
		getProducts(txn);
		txn->productViewCntr = 0;

	} else if (txn->hop_count == 2) {
		getCart(txn);

	} else if (txn->hop_count == 3) {
		convertCurrencyOfProducts(txn);
		homeHandler(txn);
	} else if (txn->hop_count == 4) {
		chooseAd(txn);

	} else if (txn->hop_count == 5) {
		returnResponse(txn);

	} else {
		printf("homeHandler doesn't know what to do for HOP %u.\n", txn->hop_count);
		returnResponse(txn);

	}
	return;
}

static void productHandler(struct http_transaction *txn) {
	printf("Call productHandler ### Hop: %u\n", txn->hop_count);

	if (txn->hop_count == 0) {
		getProduct(txn);
		txn->productViewCntr = 0;
	} else if (txn->hop_count == 1) {
		getCurrencies(txn);
	} else if (txn->hop_count == 2) {
		getCart(txn);
	} else if (txn->hop_count == 3) {
		convertCurrencyOfProduct(txn);

	} else if (txn->hop_count == 4) {
		chooseAd(txn);
	} else if (txn->hop_count == 5) {
		returnResponse(txn);
	} else {
		printf("productHandler doesn't know what to do for HOP %u.\n", txn->hop_count);
		returnResponse(txn);

	}
	return;
}

static void addToCartHandler(struct http_transaction *txn) {
	printf("Call addToCartHandler ### Hop: %u\n", txn->hop_count);
	if (txn->hop_count == 0) {
		getProduct(txn);
		txn->productViewCntr = 0;

	} else if (txn->hop_count == 1) {
		insertCart(txn);

	} else if (txn->hop_count == 2) {
		returnResponse(txn);
	} else {
		printf("addToCartHandler doesn't know what to do for HOP %u.\n", txn->hop_count);
		returnResponse(txn);
	}
}

static void viewCartHandler(struct http_transaction *txn) {
	printf("[%s()] Call viewCartHandler ### Hop: %u\n", __func__, txn->hop_count);
	if (txn->hop_count == 0) {
		getCurrencies(txn);

	} else if (txn->hop_count == 1) {
		getCart(txn);
		txn->cartItemViewCntr = 0;
		strcpy(txn->total_price.CurrencyCode, defaultCurrency);

	} else if (txn->hop_count == 2) {
		getRecommendations(txn);

	} else if (txn->hop_count == 3) {
		getShippingQuote(txn);

	} else if (txn->hop_count == 4) {
		convertCurrencyOfShippingQuote(txn);
		if (txn->get_quote_response.conversion_flag == true) {
			getCartItemInfo(txn);
			txn->hop_count++;

		} else {
			printf("[%s()] Set get_quote_response.conversion_flag as true\n", __func__);
			txn->get_quote_response.conversion_flag = true;
		}
		
	} else if (txn->hop_count == 5) {
		getCartItemInfo(txn);

	} else if (txn->hop_count == 6) {
		convertCurrencyOfCart(txn);
	} else {
		printf("[%s()] viewCartHandler doesn't know what to do for HOP %u.\n", __func__, txn->hop_count);
		returnResponse(txn);
	}
}

static void PlaceOrder(struct http_transaction *txn) {
	parsePlaceOrderRequest(txn);
	// PrintPlaceOrderRequest(txn);

	strcpy(txn->rpc_handler, "PlaceOrder");
	txn->caller_fn = FRONTEND;
	txn->next_fn = CHECKOUT_SVC;
	txn->hop_count++;
	txn->checkoutsvc_hop_cnt = 0;
}

static void placeOrderHandler(struct http_transaction *txn) {
	printf("[%s()] Call placeOrderHandler ### Hop: %u\n", __func__, txn->hop_count);

	if (txn->hop_count == 0) {
		PlaceOrder(txn);

	} else if (txn->hop_count == 1) {
		getRecommendations(txn);

	} else if (txn->hop_count == 2) {
		getCurrencies(txn);

	} else if (txn->hop_count == 3) {
		returnResponse(txn);

	} else {
		printf("[%s()] placeOrderHandler doesn't know what to do for HOP %u.\n", __func__, txn->hop_count);
		returnResponse(txn);
	}
}

static void httpRequestDispatcher(struct http_transaction *txn) {

	char *req = txn->request;
	// printf("Receive one msg: %s\n", req);
	if (strstr(req, "/1/cart/checkout") != NULL) {
		placeOrderHandler(txn);
	} else if (strstr(req, "/1/cart") != NULL) {
		if (strstr(req, "GET")) {
			viewCartHandler(txn);
		} else if (strstr(req, "POST")) {
			addToCartHandler(txn);
		} else {
			printf("No handler found in frontend\n");
			printf("%s\n", req);
		}
	} else if (strstr(req, "/1/product") != NULL) {
		productHandler(txn);
	} else if (strstr(req, "/1/setCurrency") != NULL) {
		setCurrencyHandler(txn);
	} else if (strstr(req, "/1") != NULL) {
		homeHandler(txn);
	} else {
		printf("Unknown handler. Check your HTTP Query, human!: %s\n", req);
		returnResponse(txn);
	}

	return;
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
		// printf("Receive one msg: %s\n", txn->request);
		httpRequestDispatcher(txn);

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
	// uint8_t next_node;
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
