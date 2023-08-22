// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include "lib_sidecar.h"

uint64_t get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

// You can add functions to the call sequence
// dynamically by assigning function pointers to the array elements
void add_fn_to_sequence(fn_ptr call_sequence[], int *num_fns, fn_ptr func) {
    if (*num_fns < MAX_FUNCTIONS) {
        call_sequence[(*num_fns)++] = func;
    } else {
        // Handle error, the sequence is full.
        perror("Handle error, the sequence is full");
    }
}

// Execute the Call Sequence
void execute_call_sequence(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn) {
    for (int i = 0; i < num_fns; i++) {
        call_sequence[i](txn);
    }
}

void sidecar_init(fn_ptr rx_call_sequence[], int *num_rx_fns, fn_ptr tx_call_sequence[], int *num_tx_fns) {
    // Add sidecar functions to the RX sequence
    add_fn_to_sequence(rx_call_sequence, num_rx_fns, requestLogHandler);
    add_fn_to_sequence(rx_call_sequence, num_rx_fns, HTTPSpanMiddleware);
    add_fn_to_sequence(rx_call_sequence, num_rx_fns, requestMetricsHandler);
    add_fn_to_sequence(rx_call_sequence, num_rx_fns, NewTimeoutHandler);
    add_fn_to_sequence(rx_call_sequence, num_rx_fns, ForwardedShimHandler);
    add_fn_to_sequence(rx_call_sequence, num_rx_fns, ProxyHandler);

    // Add sidecar functions to the TX sequence
    add_fn_to_sequence(tx_call_sequence, num_tx_fns, requestAppMetricsHandler);
    add_fn_to_sequence(tx_call_sequence, num_tx_fns, ConcurrencyStateHandler);

    // Initialize metrics record
    metrics.requestCountM = 0;
    metrics.responseTimeInMsecM = 0;
    metrics.appRequestCountM = 0;
    metrics.appResponseTimeInMsecM = 0;
    metrics.queueDepthM = 0;

}

void sidecar_rx(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn) {

    execute_call_sequence(call_sequence, num_fns, txn);

}

void sidecar_tx(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn) {

    execute_call_sequence(call_sequence, num_fns, txn);

    printf("METRICS REPORT || requestCountM: %ld, responseTimeInMsecM: %lu, appRequestCountM: %ld, appResponseTimeInMsecM: %lu, queueDepthM: %ld\n", \
    metrics.requestCountM, metrics.responseTimeInMsecM, metrics.appRequestCountM, metrics.appResponseTimeInMsecM, metrics.queueDepthM);

}

void requestLogHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);

    metrics.responseTimeInMsecM = get_timestamp();
}

void HTTPSpanMiddleware(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void requestMetricsHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);

    metrics.requestCountM++;
}

void NewTimeoutHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void ForwardedShimHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void ProxyHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);

    metrics.appResponseTimeInMsecM = get_timestamp();
}

void requestAppMetricsHandler(struct http_transaction *txn) {
    printf("[EGRESS]  | [%s] \n", __func__);

    uint64_t app_egress_timestamp = get_timestamp();
    metrics.appResponseTimeInMsecM = app_egress_timestamp - metrics.appResponseTimeInMsecM;
    metrics.appRequestCountM++;
}

void ConcurrencyStateHandler(struct http_transaction *txn) {
    printf("[EGRESS]  | [%s] \n", __func__);

    uint64_t fn_egress_timestamp = get_timestamp();
    metrics.responseTimeInMsecM = fn_egress_timestamp - metrics.responseTimeInMsecM;
}