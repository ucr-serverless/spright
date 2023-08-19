// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include <stdio.h>
#include <string.h>

#include "http.h"

// TRIGGER OF SIDECAR INGRESS
void sidecar_rx(struct http_transaction *txn);

// INGRESS SIDECAR MODULES
void requestLogHandler(struct http_transaction *txn);

void HTTPSpanMiddleware(struct http_transaction *txn);

// Record ingress metrics, e.g, number of requests that are routed to queue-proxy,
// response time that includes the processing time in queue proxy
void requestMetricsHandler(struct http_transaction *txn);

// Return 504 Gateway Timeout error
void NewTimeoutHandler(struct http_transaction *txn);

// Add "Forwarded" request header, denoting that this request is handed over by a reverse proxy
void ForwardedShimHandler(struct http_transaction *txn);

// Rate control; "circuit breaker"
void ProxyHandler(struct http_transaction *txn);

// TRIGGER OF SIDECAR ERESS
void sidecar_tx(struct http_transaction *txn);

// EGRESS SIDECAR MODULES
// Record egress metrics, e.g, number of requests that are routed to user containers,
// response time that only includes the processing time in user container
void requestAppMetricsHandler(struct http_transaction *txn);

// Track in-flight requests that are being processed by user container
void ConcurrencyStateHandler(struct http_transaction *txn);