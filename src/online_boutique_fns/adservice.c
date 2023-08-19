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

#define MAX_ADS_TO_SERVE 1

char *ad_name[] = {"clothing", "accessories", "footwear", "hair", "decor", "kitchen"};

static Ad getAdsByCategory(char contextKey[]) {
	if (strcmp(contextKey, "clothing") == 0) {
		Ad ad = {"/product/66VCHSJNUP", "Tank top for sale. 20 off."};
		return ad;
	} else if (strcmp(contextKey, "accessories") == 0) {
		Ad ad = {"/product/1YMWWN1N4O", "Watch for sale. Buy one, get second kit for free"};
		return ad;
	} else if (strcmp(contextKey, "footwear") == 0) {
		Ad ad = {"/product/L9ECAV7KIM", "Loafers for sale. Buy one, get second one for free"};
		return ad;
	} else if (strcmp(contextKey, "hair") == 0) {
		Ad ad = {"/product/2ZYFJ3GM2N", "Hairdryer for sale. 50 off."};
		return ad;
	} else if (strcmp(contextKey, "decor") == 0) {
		Ad ad = {"/product/0PUK6V6EV0", "Candle holder for sale. 30 off."};
		return ad;
	} else if (strcmp(contextKey, "kitchen") == 0) {
		Ad ad = {"/product/6E92ZMYYFZ", "Mug for sale. Buy two, get third one for free"};
		return ad;
	} else {
		printf("No Ad found.\n");
		Ad ad = {"", ""};
		return ad;
	}
}

static Ad getRandomAds() {
	int i;
	int ad_index;

	for (i = 0; i < MAX_ADS_TO_SERVE; i++) {
		ad_index = rand() % 6;
		if (strcmp(ad_name[ad_index], "clothing") == 0) {
			Ad ad = {"/product/66VCHSJNUP", "Tank top for sale. 20 off."};
			return ad;
		} else if (strcmp(ad_name[ad_index], "accessories") == 0) {
			Ad ad = {"/product/1YMWWN1N4O", "Watch for sale. Buy one, get second kit for free"};
			return ad;
		} else if (strcmp(ad_name[ad_index], "footwear") == 0) {
			Ad ad = {"/product/L9ECAV7KIM", "Loafers for sale. Buy one, get second one for free"};
			return ad;
		} else if (strcmp(ad_name[ad_index], "hair") == 0) {
			Ad ad = {"/product/2ZYFJ3GM2N", "Hairdryer for sale. 50 off."};
			return ad;
		} else if (strcmp(ad_name[ad_index], "decor") == 0) {
			Ad ad = {"/product/0PUK6V6EV0", "Candle holder for sale. 30 off."};
			return ad;
		} else if (strcmp(ad_name[ad_index], "kitchen") == 0) {
			Ad ad = {"/product/6E92ZMYYFZ", "Mug for sale. Buy two, get third one for free"};
			return ad;
		} else {
			printf("No Ad found.\n");
			Ad ad = {"", ""};
			return ad;
		}
	}

	printf("No Ad found.\n");
	Ad ad = {"", ""};
	return ad;
}

static AdRequest* GetContextKeys(struct http_transaction *in) {
	return &(in->ad_request);
}

static void PrintContextKeys(AdRequest* ad_request) {
	int i;
    for(i = 0; i < ad_request->num_context_keys; i++) {
        printf("context_word[%d]=%s\t\t", i + 1, ad_request->ContextKeys[i]);
    }
	printf("\n");
}

static void PrintAdResponse(struct http_transaction *in) {
	int i;
	printf("Ads in AdResponse:\n");
    for(i = 0; i < in->ad_response.num_ads; i++) {
        printf("Ad[%d] RedirectUrl: %s\tText: %s\n", i + 1, in->ad_response.Ads[i].RedirectUrl, in->ad_response.Ads[i].Text);
    }
	printf("\n");
}

static void GetAds(struct http_transaction *in) {
	printf("[GetAds] received ad request\n");

	AdRequest* ad_request = GetContextKeys(in);
	PrintContextKeys(ad_request);
	in->ad_response.num_ads = 0;

	// []*pb.Ad allAds;
	if (ad_request->num_context_keys > 0) {
		printf("Constructing Ads using received context.\n");
		int i;
		for(i = 0; i < ad_request->num_context_keys; i++) {
			printf("context_word[%d]=%s\n", i + 1, ad_request->ContextKeys[i]);
			Ad ad = getAdsByCategory(ad_request->ContextKeys[i]);

			strcpy(in->ad_response.Ads[i].RedirectUrl, ad.RedirectUrl);
			strcpy(in->ad_response.Ads[i].Text, ad.Text);
			in->ad_response.num_ads++;
		}
	} else {
		printf("No Context provided. Constructing random Ads.\n");
		Ad ad = getRandomAds();
		
		strcpy(in->ad_response.Ads[0].RedirectUrl, ad.RedirectUrl);
		strcpy(in->ad_response.Ads[0].Text, ad.Text);
		in->ad_response.num_ads++;
	}

	if (in->ad_response.num_ads == 0) {
		printf("No Ads found based on context. Constructing random Ads.\n");
		Ad ad = getRandomAds();

		strcpy(in->ad_response.Ads[0].RedirectUrl, ad.RedirectUrl);
		strcpy(in->ad_response.Ads[0].Text, ad.Text);
		in->ad_response.num_ads++;
	}

	printf("[GetAds] completed request\n");
}

static void MockAdRequest(struct http_transaction *in) {
	int num_context_keys = 2;
	int i;
	
	in->ad_request.num_context_keys = 0;
	for (i = 0; i < num_context_keys; i++) {
		in->ad_request.num_context_keys++;
		strcpy(in->ad_request.ContextKeys[i], ad_name[i]);
	}
}

static void *nf_worker(void *arg)
{
	struct http_transaction *txn = NULL;
	ssize_t bytes_written;
	ssize_t bytes_read;
	uint8_t index;
	// int ret;

	/* TODO: Careful with this pointer as it may point to a stack */
	index = (uint64_t)arg;

	while (1) {
		bytes_read = read(pipefd_rx[index][0], &txn,
		                  sizeof(struct http_transaction *));
		if (unlikely(bytes_read == -1)) {
			fprintf(stderr, "read() error: %s\n", strerror(errno));
			return NULL;
		}

		if (strcmp(txn->rpc_handler, "GetAds") == 0) {
			GetAds(txn);
		} else {
			printf("%s() is not supported\n", txn->rpc_handler);
			printf("\t\t#### Run Mock Test ####\n");
			MockAdRequest(txn);
			GetAds(txn);
			PrintAdResponse(txn);
		}

		txn->next_fn = txn->caller_fn;
		txn->caller_fn = AD_SVC;

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
