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

static int pipefd_rx[UINT8_MAX][2];
static int pipefd_tx[UINT8_MAX][2];

static int get_digits(int64_t num) {
  //returns the number of digits
  return (int)floor(log10(num));
}

static int get_digit_sum(int n) {
    return (int)(n / 10) + (n % 10);
}

static char* creditcard_validator(int64_t credit_card) {

    int digits = get_digits(credit_card);
    int sum = 0;
    int first_digits = 0;
	char* card_type;
    int i;
    digits++;

    for (i = 0; i < digits; i++) {
        if (i & 1) {
            sum += get_digit_sum(2 * (credit_card % 10));
        } else {
            sum += credit_card % 10;
        }

        if (i == digits - 2) {
            first_digits = credit_card % 10;
        } else if (i == digits - 1) {
            first_digits = first_digits + (credit_card % 10) * 10;
        }

        credit_card /= 10;
    }
    
    if (!(sum % 10)) {
        if (digits == 15 && (first_digits == 34 || first_digits == 37)) {
			card_type = "amex";
        } else if (digits == 16 && ((first_digits >= 50 && first_digits <= 55) || (first_digits >= 22 && first_digits <= 27))) {
			card_type = "mastercard";
        } else if ((digits >= 13 && digits <= 16) && (first_digits / 10 == 4)) {
			card_type = "visa";
        } else {
			card_type = "invalid";
        }
    } else {
		card_type = "invalid";
    }

    return card_type;
}

static void Charge(struct http_transaction *txn) {
	printf("[Charge] received request\n");
	ChargeRequest* in = &txn->charge_request;

	Money* amount = &in->Amount;
	char* cardNumber = in->CreditCard.CreditCardNumber;

	char* cardType;
	bool valid = false;
	cardType = creditcard_validator(strtoll(cardNumber, NULL, 10));
	if (strcmp(cardType, "invalid")) {
		valid = true;
	}

	if (!valid) { // throw InvalidCreditCard 
		printf("Credit card info is invalid\n");
		return;
	}

	// Only VISA and mastercard is accepted, 
	// other card types (AMEX, dinersclub) will
	// throw UnacceptedCreditCard error.
	if ((strcmp(cardType, "visa") != 0) && (strcmp(cardType, "mastercard") != 0)) {
		printf("Sorry, we cannot process %s credit cards. Only VISA or MasterCard is accepted.\n", cardType);
		return;
	}

	// Also validate expiration is > today.
	int32_t currentMonth = 5;
	int32_t currentYear = 2022;
	int32_t year = in->CreditCard.CreditCardExpirationYear;
	int32_t month = in->CreditCard.CreditCardExpirationMonth;
	if ((currentYear * 12 + currentMonth) > (year * 12 + month)) { // throw ExpiredCreditCard
		printf("Your credit card (ending %s) expired on %d/%d\n", cardNumber, month, year);
		return;
	}

	printf("Transaction processed: %s ending %s Amount: %s%ld.%d\n", cardType, cardNumber, amount->CurrencyCode, amount->Units, amount->Nanos);
	uuid_t binuuid; uuid_generate_random(binuuid);
	uuid_unparse(binuuid, txn->charge_response.TransactionId);

	return;
}

static void MockChargeRequest(struct http_transaction *txn) {
	strcpy(txn->charge_request.CreditCard.CreditCardNumber, "4432801561520454");
	txn->charge_request.CreditCard.CreditCardCvv = 672;
	txn->charge_request.CreditCard.CreditCardExpirationYear = 2039;
	txn->charge_request.CreditCard.CreditCardExpirationMonth = 1;

	strcpy(txn->charge_request.Amount.CurrencyCode, "USD");
	txn->charge_request.Amount.Units = 300;
	txn->charge_request.Amount.Nanos = 2;
}

static void PrintChargeResponse(struct http_transaction *txn) {
	printf("TransactionId: %s\n", txn->charge_response.TransactionId);
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

		MockChargeRequest(txn);

		Charge(txn);

		PrintChargeResponse(txn);

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
