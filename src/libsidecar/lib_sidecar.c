// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include "lib_sidecar.h"

// Initialize the EWMA structure
void ewma_init(EwmaUint64 *ewma, double alpha, uint64_t initial_value) {
    ewma->alpha = alpha;
    ewma->ema = initial_value;
    ewma->initialized = 0;
}

// Update the EWMA with a new value
void ewma_update(EwmaUint64 *ewma, uint64_t new_value) {
    if (!ewma->initialized) {
        ewma->ema = new_value;
        ewma->initialized = 1;
    } else {
        ewma->ema = (uint64_t)((1.0 - ewma->alpha) * ewma->ema + ewma->alpha * new_value);
    }
}

// Get the current EWMA value
uint64_t ewma_get(const EwmaUint64 *ewma) {
    return ewma->ema;
}

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

    double alpha = 0.2;  // Weighting factor (adjust as needed)

    // Initialize metrics record
    metrics.requestCountM = 0;
    // metrics.responseTimeInMsecM.initialized = 0;
    ewma_init(&metrics.responseTimeInMsecM, alpha, 0);
    metrics.appRequestCountM = 0;
    // metrics.appResponseTimeInMsecM.initialized = 0;
    ewma_init(&metrics.appResponseTimeInMsecM, alpha, 0);
    metrics.queueDepthM = 0;

}

void sidecar_rx(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn) {

    execute_call_sequence(call_sequence, num_fns, txn);

}

void sidecar_tx(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn) {

    execute_call_sequence(call_sequence, num_fns, txn);

    printf("METRICS REPORT || requestCountM: %ld, responseTimeInMsecM: %lu, appRequestCountM: %ld, appResponseTimeInMsecM: %lu, queueDepthM: %ld\n", \
    metrics.requestCountM, metrics.responseTimeInMsecM.ema, metrics.appRequestCountM, metrics.appResponseTimeInMsecM.ema, metrics.queueDepthM);

}

void requestLogHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);

    metrics.responseTimeInMsecM.ingress_ts = get_timestamp();
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

    metrics.appResponseTimeInMsecM.ingress_ts = get_timestamp();
}

void requestAppMetricsHandler(struct http_transaction *txn) {
    printf("[EGRESS]  | [%s] \n", __func__);

    uint64_t app_egress_timestamp = get_timestamp();
    uint64_t appResponseTimeInMsecM = app_egress_timestamp - metrics.appResponseTimeInMsecM.ingress_ts;
    ewma_update(&metrics.appResponseTimeInMsecM, appResponseTimeInMsecM);
    metrics.appRequestCountM++;
}

void ConcurrencyStateHandler(struct http_transaction *txn) {
    printf("[EGRESS]  | [%s] \n", __func__);

    uint64_t fn_egress_timestamp = get_timestamp();
    uint64_t responseTimeInMsecM = fn_egress_timestamp - metrics.responseTimeInMsecM.ingress_ts;
    ewma_update(&metrics.responseTimeInMsecM, responseTimeInMsecM);
}