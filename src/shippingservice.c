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

static int pipefd_rx[UINT8_MAX][2];
static int pipefd_tx[UINT8_MAX][2];

// Quote represents a currency value.
typedef struct _quote {
	uint32_t Dollars;
	uint32_t Cents;
} Quote;

// CreateQuoteFromFloat takes a price represented as a float and creates a Price struct.
static Quote CreateQuoteFromFloat(double value) {
	double fraction, units;
	fraction = modf(value, &units);

	Quote q = {
		.Dollars = (uint32_t) units,
		.Cents = (uint32_t) trunc(fraction * 100)
	};
	return q;
}

// quoteByCountFloat takes a number of items and generates a price quote represented as a float.
static double quoteByCountFloat(int count) {
	if (count == 0) {
		return 0;
	}
	return 8.99;
}

// CreateQuoteFromCount takes a number of items and returns a Price struct.
static Quote CreateQuoteFromCount(int count) {
	return CreateQuoteFromFloat(quoteByCountFloat(count));
}

// GetQuote produces a shipping quote (cost) in USD.
static void GetQuote(struct http_transaction *txn) {
	printf("[GetQuote] received request\n");
	
	GetQuoteRequest* in = &txn->get_quote_request;

	// 1. Our quote system requires the total number of items to be shipped.
	int count = 0;
	int i;
	// printf("num_items: %d\n", in->num_items);
	for (i = 0; i < in->num_items; i++) {
		count += in->Items[i].Quantity;
	}
	// printf("count: %d\n", count);

	// 2. Generate a quote based on the total number of items to be shipped.
	Quote quote = CreateQuoteFromCount(count);

	// 3. Generate a response.
	GetQuoteResponse* out = &txn->get_quote_response;
	strcpy(out->CostUsd.CurrencyCode, "USD");
	out->CostUsd.Units = (int64_t) quote.Dollars;
	out->CostUsd.Nanos = (int32_t) (quote.Cents * 10000000);
	
	return;
}

static void MockGetQuoteRequest(struct http_transaction *txn) {
	GetQuoteRequest* in = &txn->get_quote_request;
	in->num_items = 0;

	int i;
	for (i = 0; i < 3; i++) {
		in->Items[i].Quantity = i + 1;
		in->num_items++;
	}

	return;
}

static void PrintGetQuoteResponse(struct http_transaction *txn) {
	GetQuoteResponse* out = &txn->get_quote_response;
	printf("Shipping cost: %s %ld.%d\n", out->CostUsd.CurrencyCode, out->CostUsd.Units, out->CostUsd.Nanos);
}

// getRandomLetterCode generates a code point value for a capital letter.
static uint32_t getRandomLetterCode() {
	return 65 + (uint32_t) (rand() % 25);
}

// getRandomNumber generates a string representation of a number with the requested number of digits.
static void getRandomNumber(int digits, char *str) {
	char tmp[10];
	int i;
	for (i = 0; i < digits; i++) {
		sprintf(tmp, "%d", rand() % 10);
		strcat(str, tmp);
	}

	return;
}

// CreateTrackingId generates a tracking ID.
static void CreateTrackingId(char *salt, char* out) {

	char random_n_1[10]; getRandomNumber(3, random_n_1);
	char random_n_2[10]; getRandomNumber(7, random_n_2);
	sprintf(out, "%c%c-%ld%s-%ld%s",
		getRandomLetterCode(),
		getRandomLetterCode(),
		strlen(salt),
		random_n_1,
		strlen(salt)/2,
		random_n_2
	);

	return;
}

// ShipOrder mocks that the requested items will be shipped.
// It supplies a tracking ID for notional lookup of shipment delivery status.
static void ShipOrder(struct http_transaction *txn) {
	printf("[ShipOrder] received request\n");
	ShipOrderRequest *in = &txn->ship_order_request;
	
	// 1. Create a Tracking ID
	char baseAddress[100] = "";
    strcat(baseAddress, in->address.StreetAddress); strcat (baseAddress, ", ");
	strcat(baseAddress, in->address.City); strcat (baseAddress, ", ");
	strcat(baseAddress, in->address.State);

	char id[100];
	CreateTrackingId(baseAddress, id);

	// 2. Generate a response.
	ShipOrderResponse *out = &txn->ship_order_response;
	strcpy(out->TrackingId, id);
	return;
}

static void MockShipOrderRequest(struct http_transaction *txn) {
	ShipOrderRequest *in = &txn->ship_order_request;
	strcpy(in->address.StreetAddress, "1600 Amphitheatre Parkway");
	strcpy(in->address.City, "Mountain View");
	strcpy(in->address.State, "CA");
	strcpy(in->address.Country, "United States");
	in->address.ZipCode = 94043;
}

static void PrintShipOrderResponse(struct http_transaction *txn) {
	ShipOrderResponse *out = &txn->ship_order_response;
	printf("Tracking ID: %s\n", out->TrackingId);
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

		MockShipOrderRequest(txn);

		ShipOrder(txn);

		PrintShipOrderResponse(txn);

		MockGetQuoteRequest(txn);
		GetQuote(txn);
		PrintGetQuoteResponse(txn);

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
	uint8_t next_node;
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

			txn->hop_count++;

			if (likely(txn->hop_count <
			           cfg->route[txn->route_id].length)) {
				next_node =
				cfg->route[txn->route_id].node[txn->hop_count];
			} else {
				next_node = 0;
			}

			ret = io_tx(txn, next_node);
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
