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
#include "c_lib.h"

static int pipefd_rx[UINT8_MAX][2];
static int pipefd_tx[UINT8_MAX][2];

static int compare_e(void* left, void* right ) {
    return strcmp((const char *)left, (const char *)right);
}

struct clib_map* LocalCartStore;

static void PrintUserCart(Cart *cart) {
    log_info("Cart for user %s: ", cart->UserId);
    log_info("## %d items in the cart: ", cart->num_items);
    int i;
    for (i = 0; i < cart->num_items; i++) {
        log_info("\t%d. ProductId: %s \tQuantity: %d", i + 1, cart->Items[i].ProductId, cart->Items[i].Quantity);
    }
    printf("\n");
    return;
}

static void PrintLocalCartStore() {
    log_info("\t\t #### PrintLocalCartStore ####");

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
    log_info("AddItemAsync called with userId=%s, productId=%s, quantity=%d", userId, productId, quantity);

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
        log_info("Add new carts for user %s", userId);
        char *key = clib_strdup(userId);
        int key_length = (int)strlen(key) + 1;
        newCart.num_items = 1;
        log_info("Inserting [%s -> %s]", key, newCart.UserId);
        insert_c_map(LocalCartStore, key, key_length, &newCart, sizeof(Cart));
        free(key);
    } else {
        log_info("Found carts for user %s", userId);
        int cnt = 0;
        int i;
        for (i = 0; i < ((Cart*)cart)->num_items; i++) {
            if (strcmp(((Cart*)cart)->Items[i].ProductId, productId) == 0) { // If the item exists, we update its quantity
                log_info("Update carts for user %s - the item exists, we update its quantity", userId);
                ((Cart*)cart)->Items[i].Quantity++;
            } else {
                cnt++;
            }
        }

        if (cnt == ((Cart*)cart)->num_items) { // The item doesn't exist, we update it into DB
            log_info("Update carts for user %s - The item doesn't exist, we update it into DB", userId);
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
    log_info("[AddItem] received request");

    AddItemRequest *in = &txn->add_item_request;
    AddItemAsync(in->UserId, in->Item.ProductId, in->Item.Quantity);
    return;
}

static void GetCartAsync(struct http_transaction *txn) {
    GetCartRequest *in = &txn->get_cart_request;
    Cart *out = &txn->get_cart_response;
    log_info("[GetCart] GetCartAsync called with userId=%s", in->UserId);

    void *cart;
    if (clib_true != find_c_map(LocalCartStore, in->UserId, &cart)) {
        log_info("No carts for user %s", in->UserId);
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
    log_info("\t\t#### PrintGetCartResponse ####");
    Cart *out = &txn->get_cart_response;
    log_info("Cart for user %s: ", out->UserId);
    int i;
    for (i = 0; i < out->num_items; i++) {
        log_info("\t%d. ProductId: %s \tQuantity: %d", i + 1, out->Items[i].ProductId, out->Items[i].Quantity);
    }
    printf("\n");
    return;
}

static void EmptyCartAsync(struct http_transaction *txn) {
    EmptyCartRequest *in = &txn->empty_cart_request;
    log_info("EmptyCartAsync called with userId=%s", in->UserId);

    void *cart;
    if (clib_true != find_c_map(LocalCartStore, in->UserId, &cart)) {
        log_info("No carts for user %s", in->UserId);
        // out->num_items = -1;
        return;
    } else {
        int i;
        for (i = 0; i < ((Cart*)cart)->num_items; i++) {
            log_info("Clean up item %d", i + 1);
            strcpy((*((Cart**)(&cart)))->Items[i].ProductId, "");
            ((*((Cart**)(&cart))))->Items[i].Quantity = 0;
        }
        PrintUserCart((Cart*)cart);
        return;
    }
}

static void EmptyCart(struct http_transaction *txn) {
    log_info("[EmptyCart] received request");
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
            log_error("read() error: %s", strerror(errno));
            return NULL;
        }

        if (strcmp(txn->rpc_handler, "AddItem") == 0) {
            AddItem(txn);
        } else if (strcmp(txn->rpc_handler, "GetCart") == 0) {
            GetCart(txn);
        } else if (strcmp(txn->rpc_handler, "EmptyCart") == 0) {
            EmptyCart(txn);
        } else {
            log_info("%s() is not supported", txn->rpc_handler);
            log_info("\t\t#### Run Mock Test ####");
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
    // uint8_t next_node;
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

    LocalCartStore = new_c_map(compare_e, NULL, NULL);
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
