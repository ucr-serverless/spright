// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include <stdio.h>
#include <string.h>

#include "http.h"
#include "utility.h"

char defaultCurrency[5];

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