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

#include <stdio.h>
#include <string.h>

#include "http.h"
#include "utility.h"

extern char defaultCurrency[5];

void chooseAd(struct http_transaction *txn);

void returnResponse(struct http_transaction *txn);

void getCurrencies(struct http_transaction *txn);

// Get a list of products
void getProducts(struct http_transaction *txn);

void getCart(struct http_transaction *txn);

// Convert currency for a list of products
void convertCurrencyOfProducts(struct http_transaction *txn);

// Get a single product
void getProduct(struct http_transaction *txn);

void getRecommendations(struct http_transaction *txn);

// Convert currency for a product
void convertCurrencyOfProduct(struct http_transaction *txn);

void insertCart(struct http_transaction *txn);

void getShippingQuote(struct http_transaction *txn);

// Convert currency for products in the cart
void convertCurrencyOfCart(struct http_transaction *txn);

void getCartItemInfo(struct http_transaction *txn);

// Convert currency for a ShippingQuote
void convertCurrencyOfShippingQuote(struct http_transaction *txn);

void calculateTotalPrice(struct http_transaction *txn);