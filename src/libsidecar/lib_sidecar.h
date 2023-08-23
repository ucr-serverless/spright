// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "http.h"

// Exponentially Weighted Moving Average (EWMA) structure for response time
typedef struct {
    double alpha;       // Weighting factor (0 < alpha < 1)
    uint64_t ema;       // Exponentially Weighted Moving Average
    uint64_t ingress_ts;      // Use to cache the start timestamp
    int initialized;    // Flag to track if the EMA has been initialized
} EwmaUint64;

struct stats {
    int64_t requestCountM;
    EwmaUint64 responseTimeInMsecM;
    int64_t appRequestCountM;
    EwmaUint64 appResponseTimeInMsecM;
    int64_t queueDepthM;
};

struct stats metrics;

uint64_t get_timestamp();

// Define a function pointer type that matches the 
// signature of the functions you want to call in the sequence.
// This assumes that the functions you want to call take no arguments 
// and return void. You can modify this definition to match your specific requirements.
typedef void (*fn_ptr)(struct http_transaction *txn);

// Max functions allowed in a sequence
#define MAX_FUNCTIONS 10

// Add Functions to the Call Sequence
void add_fn_to_sequence(fn_ptr call_sequence[], int *num_fns, fn_ptr func);

// Execute the Call Sequence
void execute_call_sequence(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn);

// Initialize the RX/TX call sequence and add sidecar functions
void sidecar_init(fn_ptr rx_call_sequence[], int *num_rx_fns, fn_ptr tx_call_sequence[], int *num_tx_fns);

// TRIGGER OF SIDECAR INGRESS
void sidecar_rx(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn);

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
void sidecar_tx(fn_ptr call_sequence[], int num_fns, struct http_transaction *txn);

// EGRESS SIDECAR MODULES
// Record egress metrics, e.g, number of requests that are routed to user containers,
// response time that only includes the processing time in user container
void requestAppMetricsHandler(struct http_transaction *txn);

// Track in-flight requests that are being processed by user container
void ConcurrencyStateHandler(struct http_transaction *txn);