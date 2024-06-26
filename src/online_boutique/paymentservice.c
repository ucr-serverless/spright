/*
# Copyright 2022 University of California, Riverside
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

#include <errno.h>
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
    log_info("[Charge] received request");
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
        log_info("Credit card info is invalid");
        return;
    }

    // Only VISA and mastercard is accepted, 
    // other card types (AMEX, dinersclub) will
    // throw UnacceptedCreditCard error.
    if ((strcmp(cardType, "visa") != 0) && (strcmp(cardType, "mastercard") != 0)) {
        log_info("Sorry, we cannot process %s credit cards. Only VISA or MasterCard is accepted.", cardType);
        return;
    }

    // Also validate expiration is > today.
    int32_t currentMonth = 5;
    int32_t currentYear = 2022;
    int32_t year = in->CreditCard.CreditCardExpirationYear;
    int32_t month = in->CreditCard.CreditCardExpirationMonth;
    if ((currentYear * 12 + currentMonth) > (year * 12 + month)) { // throw ExpiredCreditCard
        log_info("Your credit card (ending %s) expired on %d/%d", cardNumber, month, year);
        return;
    }

    log_info("Transaction processed: %s ending %s Amount: %s%ld.%d", cardType, cardNumber, amount->CurrencyCode, amount->Units, amount->Nanos);
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
    log_info("TransactionId: %s", txn->charge_response.TransactionId);
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
            log_error("read() error: %s", strerror(errno));
            return NULL;
        }

        if (strcmp(txn->rpc_handler, "Charge") == 0) {
            Charge(txn);
        } else {
            log_info("%s() is not supported", txn->rpc_handler);
            log_info("\t\t#### Run Mock Test ####");
            MockChargeRequest(txn);
            PrintChargeResponse(txn);
        }

        txn->next_fn = txn->caller_fn;
        txn->caller_fn = PAYMENT_SVC;

        bytes_written = write(pipefd_tx[index][1], &txn,
                              sizeof(struct http_transaction *));
        if (unlikely(bytes_written == -1)) {
            log_error("write() error: %s", strerror(errno));
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

    for (i = 0; ; i = (i + 1) % cfg->nf[fn_id - 1].n_threads) {
        ret = io_rx((void **)&txn);
        if (unlikely(ret == -1)) {
            log_error("io_rx() error");
            return NULL;
        }

        bytes_written = write(pipefd_rx[i][1], &txn,
                              sizeof(struct http_transaction *));
        if (unlikely(bytes_written == -1)) {
            log_error("write() error: %s", strerror(errno));
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
        log_error("epoll_create1() error: %s", strerror(errno));
        return NULL;
    }

    for (i = 0; i < cfg->nf[fn_id - 1].n_threads; i++) {
        ret = set_nonblocking(pipefd_tx[i][0]);
        if (unlikely(ret == -1)) {
            return NULL;
        }

        event[0].events = EPOLLIN;
        event[0].data.fd = pipefd_tx[i][0];

        ret = epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd_tx[i][0],
                        &event[0]);
        if (unlikely(ret == -1)) {
            log_error("epoll_ctl() error: %s",
                    strerror(errno));
            return NULL;
        }
    }

    while (1) {
        n_fds = epoll_wait(epfd, event, cfg->nf[fn_id - 1].n_threads,
                           -1);
        if (unlikely(n_fds == -1)) {
            log_error("epoll_wait() error: %s",
                    strerror(errno));
            return NULL;
        }

        for (i = 0; i < n_fds; i++) {
            bytes_read = read(event[i].data.fd, &txn,
                              sizeof(struct http_transaction *));
            if (unlikely(bytes_read == -1)) {
                log_error("read() error: %s",
                        strerror(errno));
                return NULL;
            }

            log_debug("Route id: %u, Hop Count %u, Next Hop: %u, Next Fn: %u, \
                Caller Fn: %s (#%u), RPC Handler: %s()", 
                txn->route_id, txn->hop_count,
                cfg->route[txn->route_id].hop[txn->hop_count],
                txn->next_fn, txn->caller_nf, txn->caller_fn, txn->rpc_handler);

            ret = io_tx(txn, txn->next_fn);
            if (unlikely(ret == -1)) {
                log_error("io_tx() error");
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

    fn_id = nf_id;

    memzone = rte_memzone_lookup(MEMZONE_NAME);
    if (unlikely(memzone == NULL)) {
        log_error("rte_memzone_lookup() error");
        return -1;
    }

    cfg = memzone->addr;

    ret = io_init();
    if (unlikely(ret == -1)) {
        log_error("io_init() error");
        return -1;
    }

    for (i = 0; i < cfg->nf[fn_id - 1].n_threads; i++) {
        ret = pipe(pipefd_rx[i]);
        if (unlikely(ret == -1)) {
            log_error("pipe() error: %s", strerror(errno));
            return -1;
        }

        ret = pipe(pipefd_tx[i]);
        if (unlikely(ret == -1)) {
            log_error("pipe() error: %s", strerror(errno));
            return -1;
        }
    }

    ret = pthread_create(&thread_rx, NULL, &nf_rx, NULL);
    if (unlikely(ret != 0)) {
        log_error("pthread_create() error: %s", strerror(ret));
        return -1;
    }

    ret = pthread_create(&thread_tx, NULL, &nf_tx, NULL);
    if (unlikely(ret != 0)) {
        log_error("pthread_create() error: %s", strerror(ret));
        return -1;
    }

    for (i = 0; i < cfg->nf[fn_id - 1].n_threads; i++) {
        ret = pthread_create(&thread_worker[i], NULL, &nf_worker,
                             (void *)(uint64_t)i);
        if (unlikely(ret != 0)) {
            log_error("pthread_create() error: %s",
                    strerror(ret));
            return -1;
        }
    }

    for (i = 0; i < cfg->nf[fn_id - 1].n_threads; i++) {
        ret = pthread_join(thread_worker[i], NULL);
        if (unlikely(ret != 0)) {
            log_error("pthread_join() error: %s",
                    strerror(ret));
            return -1;
        }
    }

    ret = pthread_join(thread_rx, NULL);
    if (unlikely(ret != 0)) {
        log_error("pthread_join() error: %s", strerror(ret));
        return -1;
    }

    ret = pthread_join(thread_tx, NULL);
    if (unlikely(ret != 0)) {
        log_error("pthread_join() error: %s", strerror(ret));
        return -1;
    }

    for (i = 0; i < cfg->nf[fn_id - 1].n_threads; i++) {
        ret = close(pipefd_rx[i][0]);
        if (unlikely(ret == -1)) {
            log_error("close() error: %s", strerror(errno));
            return -1;
        }

        ret = close(pipefd_rx[i][1]);
        if (unlikely(ret == -1)) {
            log_error("close() error: %s", strerror(errno));
            return -1;
        }

        ret = close(pipefd_tx[i][0]);
        if (unlikely(ret == -1)) {
            log_error("close() error: %s", strerror(errno));
            return -1;
        }

        ret = close(pipefd_tx[i][1]);
        if (unlikely(ret == -1)) {
            log_error("close() error: %s", strerror(errno));
            return -1;
        }
    }

    ret = io_exit();
    if (unlikely(ret == -1)) {
        log_error("io_exit() error");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    log_set_level_from_env();

    uint8_t nf_id;
    int ret;

    ret = rte_eal_init(argc, argv);
    if (unlikely(ret == -1)) {
        log_error("rte_eal_init() error: %s",
                rte_strerror(rte_errno));
        goto error_0;
    }

    argc -= ret;
    argv += ret;

    if (unlikely(argc == 1)) {
        log_error("Network Function ID not provided");
        goto error_1;
    }

    errno = 0;
    nf_id = strtol(argv[1], NULL, 10);
    if (unlikely(errno != 0 || nf_id < 1)) {
        log_error("Invalid value for Network Function ID");
        goto error_1;
    }

    ret = nf(nf_id);
    if (unlikely(ret == -1)) {
        log_error("nf() error");
        goto error_1;
    }

    ret = rte_eal_cleanup();
    if (unlikely(ret < 0)) {
        log_error("rte_eal_cleanup() error: %s",
                rte_strerror(-ret));
        goto error_0;
    }

    return 0;

error_1:
    rte_eal_cleanup();
error_0:
    return 1;
}
