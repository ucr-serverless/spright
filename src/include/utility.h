/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef UTILITY_H
#define UTILITY_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"

#define NANOSMIN -999999999
#define NANOSMAX +999999999
#define NANOSMOD 1000000000

void PrintAdResponse(struct http_transaction *in);

void PrintProduct(Product *p);
void PrintListProductsResponse(struct http_transaction *txn);
void PrintGetProductResponse(struct http_transaction *txn);
void PrintSearchProductsResponse(struct http_transaction *txn);

void PrintSupportedCurrencies (struct http_transaction *in);
void PrintConversionResult(struct http_transaction *in);
void MockCurrencyConversionRequest(struct http_transaction *in);

void PrintGetCartResponse(struct http_transaction *txn);

void PrintProductView(struct http_transaction *txn);

void PrintListRecommendationsResponse(struct http_transaction *txn);

void PrintShipOrderResponse(struct http_transaction *txn);
void PrintGetQuoteResponse(struct http_transaction *txn);

void PrintTotalPrice(struct http_transaction *txn);

void Sum(Money *total, Money *add);
void MultiplySlow(Money *total, uint32_t n);

void PrintPlaceOrderRequest(struct http_transaction *txn);
void parsePlaceOrderRequest(struct http_transaction *txn);

char* httpQueryParser(char* req);

#endif /* UTILITY_H */
