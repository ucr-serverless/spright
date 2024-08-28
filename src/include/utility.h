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

#ifndef UTILITY_H
#define UTILITY_H

#include <arpa/inet.h>
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

void PrintSupportedCurrencies(struct http_transaction *in);
void PrintConversionResult(struct http_transaction *in);
void printMoney(Money *money);
void printCurrencyConversionRequest(CurrencyConversionRequest *request);
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

char *httpQueryParser(char *req);

/*
 * A simple key-value table for function-node mapping
 * TODO: revisit the implementation for concurrent access
 */
void set_node(uint8_t fn_id, uint8_t node_idx);
uint8_t get_node(uint8_t fn_id);
void delete_node(uint8_t fn_id);
void print_ip_address(struct in_addr *ip);
void print_rt_table();

#endif /* UTILITY_H */
