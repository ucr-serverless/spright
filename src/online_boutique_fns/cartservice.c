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
#include "c_lib.h"

static int pipefd_rx[UINT8_MAX][2];
static int pipefd_tx[UINT8_MAX][2];

static int compare_e(void* left, void* right ) {
    return strcmp((const char *)left, (const char *)right);
}

struct clib_map* LocalCartStore;

static void PrintUserCart(Cart *cart) {
	printf("Cart for user %s: \n", cart->UserId);
	printf("## %d items in the cart: ", cart->num_items);
	int i;
	for (i = 0; i < cart->num_items; i++) {
		printf("\t%d. ProductId: %s \tQuantity: %d\n", i + 1, cart->Items[i].ProductId, cart->Items[i].Quantity);
	}
	printf("\n");
	return;
}

static void PrintLocalCartStore() {
	printf("\t\t #### PrintLocalCartStore ####\n");

	struct clib_iterator *myItr;
	struct clib_object *pElement;
	myItr = new_iterator_c_map(LocalCartStore);
	pElement = myItr->get_next(myItr);

	while (pElement) {
		void* cart = myItr->get_value(pElement);
		PrintUserCart((Cart*)cart);
		free(cart);
		pElement = myItr->get_next(myItr);
	}
	delete_iterator_c_map(myItr);
	printf("\n");
}

static void AddItemAsync(char *userId, char *productId, int32_t quantity) {
	printf("AddItemAsync called with userId=%s, productId=%s, quantity=%d\n", userId, productId, quantity);

	Cart newCart = {
		.UserId = "",
		.Items = {
			{
				.ProductId = "",
				.Quantity = quantity
			}
		}
	};

	strcpy(newCart.UserId, userId);
	strcpy(newCart.Items[0].ProductId, productId);

	void* cart;
	if (clib_true != find_c_map(LocalCartStore, userId, &cart)) {
		printf("Add new carts for user %s\n", userId);
		char *key = clib_strdup(userId);
		int key_length = (int)strlen(key) + 1;
		newCart.num_items = 1;
		printf("Inserting [%s -> %s]\n", key, newCart.UserId);
		insert_c_map(LocalCartStore, key, key_length, &newCart, sizeof(Cart));
		free(key);
	} else {
		printf("Found carts for user %s\n", userId);
		int cnt = 0;
		int i;
		for (i = 0; i < ((Cart*)cart)->num_items; i++) {
			if (strcmp(((Cart*)cart)->Items[i].ProductId, productId) == 0) { // If the item exists, we update its quantity
				printf("Update carts for user %s - the item exists, we update its quantity\n", userId);
				((Cart*)cart)->Items[i].Quantity++;
			} else {
				cnt++;
			}
		}

		if (cnt == ((Cart*)cart)->num_items) { // The item doesn't exist, we update it into DB
			printf("Update carts for user %s - The item doesn't exist, we update it into DB\n", userId);
			((Cart*)cart)->num_items++;
			strcpy(((Cart*)cart)->Items[((Cart*)cart)->num_items].ProductId, productId);
			((Cart*)cart)->Items[((Cart*)cart)->num_items].Quantity = quantity;
		}
	}
	return;
}

static void MockAddItemRequest(struct http_transaction *txn) {
	AddItemRequest *in = &txn->add_item_request;
	strcpy(in->UserId, "spright-online-boutique");
	strcpy(in->Item.ProductId, "OLJCESPC7Z");
	in->Item.Quantity = 5;
	return;
}

static void AddItem(struct http_transaction *txn) {
	printf("[AddItem] received request\n");

	AddItemRequest *in = &txn->add_item_request;
	AddItemAsync(in->UserId, in->Item.ProductId, in->Item.Quantity);
	return;
}

static void GetCartAsync(struct http_transaction *txn) {
	GetCartRequest *in = &txn->get_cart_request;
	Cart *out = &txn->get_cart_response;
	printf("[GetCart] GetCartAsync called with userId=%s\n", in->UserId);

	void *cart;
	if (clib_true != find_c_map(LocalCartStore, in->UserId, &cart)) {
		printf("No carts for user %s\n", in->UserId);
		out->num_items = 0;
		return;
	} else {
		*out = *(Cart*)cart;
		return;
	}
}

static void GetCart(struct http_transaction *txn){
	GetCartAsync(txn);
	return;
}

static void MockGetCartRequest(struct http_transaction *txn) {
	GetCartRequest *in = &txn->get_cart_request;
	strcpy(in->UserId, "spright-online-boutique");
	return;
}

static void PrintGetCartResponse(struct http_transaction *txn) {
	printf("\t\t#### PrintGetCartResponse ####\n");
	Cart *out = &txn->get_cart_response;
	printf("Cart for user %s: \n", out->UserId);
	int i;
	for (i = 0; i < out->num_items; i++) {
		printf("\t%d. ProductId: %s \tQuantity: %d\n", i + 1, out->Items[i].ProductId, out->Items[i].Quantity);
	}
	printf("\n");
	return;
}

static void EmptyCartAsync(struct http_transaction *txn) {
	EmptyCartRequest *in = &txn->empty_cart_request;
	printf("EmptyCartAsync called with userId=%s\n", in->UserId);

	void *cart;
	if (clib_true != find_c_map(LocalCartStore, in->UserId, &cart)) {
		printf("No carts for user %s\n", in->UserId);
		// out->num_items = -1;
		return;
	} else {
		int i;
		for (i = 0; i < ((Cart*)cart)->num_items; i++) {
			printf("Clean up item %d\n", i + 1);
			strcpy((*((Cart**)(&cart)))->Items[i].ProductId, "");
			((*((Cart**)(&cart))))->Items[i].Quantity = 0;
		}
		PrintUserCart((Cart*)cart);
		return;
	}
}

static void EmptyCart(struct http_transaction *txn) {
	printf("[EmptyCart] received request\n");
	EmptyCartAsync(txn);
	return;
}

static void MockEmptyCartRequest(struct http_transaction *txn) {
	EmptyCartRequest *in = &txn->empty_cart_request;
	strcpy(in->UserId, "spright-online-boutique");
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

		if (strcmp(txn->rpc_handler, "AddItem") == 0) {
			AddItem(txn);
		} else if (strcmp(txn->rpc_handler, "GetCart") == 0) {
			GetCart(txn);
		} else if (strcmp(txn->rpc_handler, "EmptyCart") == 0) {
			EmptyCart(txn);
		} else {
			printf("%s() is not supported\n", txn->rpc_handler);
			printf("\t\t#### Run Mock Test ####\n");
			MockAddItemRequest(txn);
			AddItem(txn);
			PrintLocalCartStore();

			MockGetCartRequest(txn);
			GetCart(txn);
			PrintGetCartResponse(txn);

			MockEmptyCartRequest(txn);
			EmptyCart(txn);
			PrintLocalCartStore();
		}
		
		txn->next_fn = txn->caller_fn;
		txn->caller_fn = CART_SVC;

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

			// txn->hop_count++;

			// if (likely(txn->hop_count <
			//            cfg->route[txn->route_id].length)) {
			// 	next_node =
			// 	cfg->route[txn->route_id].node[txn->hop_count];
			// } else {
			// 	next_node = 0;
			// }

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

	LocalCartStore = new_c_map(compare_e, NULL, NULL);
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
