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
        log_info("No Ad found.");
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
            log_info("No Ad found.");
            Ad ad = {"", ""};
            return ad;
        }
    }

    log_info("No Ad found.");
    Ad ad = {"", ""};
    return ad;
}

static AdRequest* GetContextKeys(struct http_transaction *in) {
    return &(in->ad_request);
}

static void PrintContextKeys(AdRequest* ad_request) {
    int i;
    for(i = 0; i < ad_request->num_context_keys; i++) {
        log_info("context_word[%d]=%s\t\t", i + 1, ad_request->ContextKeys[i]);
    }
    printf("\n");
}

static void PrintAdResponse(struct http_transaction *in) {
    int i;
    log_info("Ads in AdResponse:");
    for(i = 0; i < in->ad_response.num_ads; i++) {
        log_info("Ad[%d] RedirectUrl: %s\tText: %s", i + 1, in->ad_response.Ads[i].RedirectUrl, in->ad_response.Ads[i].Text);
    }
    printf("\n");
}

static void GetAds(struct http_transaction *in) {
    log_info("[GetAds] received ad request");

    AdRequest* ad_request = GetContextKeys(in);
    PrintContextKeys(ad_request);
    in->ad_response.num_ads = 0;

    // []*pb.Ad allAds;
    if (ad_request->num_context_keys > 0) {
        log_info("Constructing Ads using received context.");
        int i;
        for(i = 0; i < ad_request->num_context_keys; i++) {
            log_info("context_word[%d]=%s", i + 1, ad_request->ContextKeys[i]);
            Ad ad = getAdsByCategory(ad_request->ContextKeys[i]);

            strcpy(in->ad_response.Ads[i].RedirectUrl, ad.RedirectUrl);
            strcpy(in->ad_response.Ads[i].Text, ad.Text);
            in->ad_response.num_ads++;
        }
    } else {
        log_info("No Context provided. Constructing random Ads.");
        Ad ad = getRandomAds();
        
        strcpy(in->ad_response.Ads[0].RedirectUrl, ad.RedirectUrl);
        strcpy(in->ad_response.Ads[0].Text, ad.Text);
        in->ad_response.num_ads++;
    }

    if (in->ad_response.num_ads == 0) {
        log_info("No Ads found based on context. Constructing random Ads.");
        Ad ad = getRandomAds();

        strcpy(in->ad_response.Ads[0].RedirectUrl, ad.RedirectUrl);
        strcpy(in->ad_response.Ads[0].Text, ad.Text);
        in->ad_response.num_ads++;
    }

    log_info("[GetAds] completed request");
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

    /* TODO: Careful with this pointer as it may point to a stack */
    index = (uint64_t)arg;

    while (1) {
        bytes_read = read(pipefd_rx[index][0], &txn,
                          sizeof(struct http_transaction *));
        if (unlikely(bytes_read == -1)) {
            log_error("read() error: %s", strerror(errno));
            return NULL;
        }

        if (strcmp(txn->rpc_handler, "GetAds") == 0) {
            GetAds(txn);
        } else {
            log_warn("%s() is not supported", txn->rpc_handler);
            log_info("\t\t#### Run Mock Test ####");
            MockAdRequest(txn);
            GetAds(txn);
            PrintAdResponse(txn);
        }

        txn->next_fn = txn->caller_fn;
        txn->caller_fn = AD_SVC;

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
        ret = fcntl(pipefd_tx[i][0], F_SETFL, O_NONBLOCK);
        if (unlikely(ret == -1)) {
            log_error("fcntl() error: %s", strerror(errno));
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
