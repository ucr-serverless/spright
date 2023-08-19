// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include "lib_sidecar.h"

void sidecar_rx(struct http_transaction *txn) {
    requestLogHandler(txn);

    HTTPSpanMiddleware(txn);

    requestMetricsHandler(txn);

    NewTimeoutHandler(txn);

    ForwardedShimHandler(txn);

    ProxyHandler(txn);

}

void sidecar_tx(struct http_transaction *txn) {
    requestAppMetricsHandler(txn);

    ConcurrencyStateHandler(txn);
}

void requestLogHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void HTTPSpanMiddleware(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void requestMetricsHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void NewTimeoutHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void ForwardedShimHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void ProxyHandler(struct http_transaction *txn) {
    printf("[INGRESS] | [%s] \n", __func__);
}

void requestAppMetricsHandler(struct http_transaction *txn) {
    printf("[EGRESS]  | [%s] \n", __func__);
}

void ConcurrencyStateHandler(struct http_transaction *txn) {
    printf("[EGRESS]  | [%s] \n", __func__);
}