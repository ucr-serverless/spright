// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#include "shm_rpc.h"

char defaultCurrency[5] = "CAD";

void returnResponse(struct http_transaction *txn) {
	txn->hop_count += 100;
	txn->next_fn = GATEWAY;
	
	// PrintSupportedCurrencies(txn);
	// PrintListProductsResponse(txn);
	// PrintGetCartResponse(txn);
	// PrintProductView(txn);
	// PrintGetProductResponse(txn);
	// PrintListRecommendationsResponse(txn);
	// PrintGetQuoteResponse(txn);
	// PrintAdResponse(txn);
	PrintTotalPrice(txn);
}

void chooseAd(struct http_transaction *txn) {
	strcpy(txn->rpc_handler, "GetAds");
	txn->caller_fn = FRONTEND;
	txn->next_fn = AD_SVC;
	txn->hop_count++;
}

void getCurrencies(struct http_transaction *txn) {
	strcpy(txn->rpc_handler, "GetSupportedCurrencies");
	txn->caller_fn = FRONTEND;
	txn->next_fn = CURRENCY_SVC;
	txn->hop_count++;
}

// Get a list of products
void getProducts(struct http_transaction *txn) {
	strcpy(txn->rpc_handler, "ListProducts");
	txn->caller_fn = FRONTEND;
	txn->next_fn = PRODUCTCATA_SVC;
	txn->hop_count++;
}

void getCart(struct http_transaction *txn) {
	strcpy(txn->get_cart_request.UserId, "ucr-students");

	strcpy(txn->rpc_handler, "GetCart");
	txn->caller_fn = FRONTEND;
	txn->next_fn = CART_SVC;
	txn->hop_count++;
}

// Convert currency for a list of products
void convertCurrencyOfProducts(struct http_transaction *txn) {
	if (strcmp(defaultCurrency, "USD") == 0) {
		printf("Default Currency is USD. Skip convertCurrency\n");
		int i = 0;
		for (i = 0; i < txn->list_products_response.num_products; i++) {
			txn->product_view[i].Item = txn->list_products_response.Products[i];
			txn->product_view[i].Price = txn->list_products_response.Products[i].PriceUsd;
		}
		// returnResponse(txn);
		txn->hop_count++;
		return;
	} else {
		printf("Default Currency is %s. Do convertCurrency\n", defaultCurrency);
		if (txn->productViewCntr != 0) {
			txn->product_view[txn->productViewCntr - 1].Item = txn->list_products_response.Products[txn->productViewCntr - 1];
			txn->product_view[txn->productViewCntr - 1].Price = txn->currency_conversion_result;
		}

		int size = sizeof(txn->product_view)/sizeof(txn->product_view[0]);
		if (txn->productViewCntr < size) {
			strcpy(txn->currency_conversion_req.ToCode, defaultCurrency);
			strcpy(txn->currency_conversion_req.From.CurrencyCode, txn->list_products_response.Products[txn->productViewCntr].PriceUsd.CurrencyCode);
			txn->currency_conversion_req.From.Units = txn->list_products_response.Products[txn->productViewCntr].PriceUsd.Units;
			txn->currency_conversion_req.From.Nanos = txn->list_products_response.Products[txn->productViewCntr].PriceUsd.Nanos;

			strcpy(txn->rpc_handler, "Convert");
			txn->caller_fn = FRONTEND;
			txn->next_fn = CURRENCY_SVC;

			txn->productViewCntr++;
			return;
		} else {
			// returnResponse(txn);
			txn->hop_count++;
			return;
		}
	}
}

// Get a single product
void getProduct(struct http_transaction *txn) {
	char *query = httpQueryParser(txn->request);
	char *req = txn->request;

	if (strstr(req, "/1/cart?") != NULL && strstr(req, "POST")) {
		// printf("Query : %s\n", query);
		char *start_of_product_id = strtok(query, "&");
		strcpy(txn->get_product_request.Id, strchr(start_of_product_id, '=') + 1);
		printf("Product ID: %s\n", txn->get_product_request.Id);
		// product_id=66VCHSJNUP&quantity=1
		// returnResponse(txn); return;
	} else if (strstr(req, "/1/product") != NULL) {
		strcpy(txn->get_product_request.Id, query);
		printf("Product ID: %s\n", txn->get_product_request.Id);
	} else {
		printf("HTTP Query cannot be parsed!\n");
		printf("\t#### %s\n", query);
		returnResponse(txn); return;
	}

	strcpy(txn->rpc_handler, "GetProduct");
	txn->caller_fn = FRONTEND;
	txn->next_fn = PRODUCTCATA_SVC;
	txn->hop_count++;
}

void getRecommendations(struct http_transaction *txn) {
	strcpy(txn->list_recommendations_request.ProductId, txn->get_product_request.Id);

	strcpy(txn->rpc_handler, "ListRecommendations");
	txn->caller_fn = FRONTEND;
	txn->next_fn = RECOMMEND_SVC;
	txn->hop_count++;
}

// Convert currency for a product
void convertCurrencyOfProduct(struct http_transaction *txn) {
	if (strcmp(defaultCurrency, "USD") == 0) {
		printf("Default Currency is USD. Skip convertCurrencyOfProduct\n");
		txn->product_view[0].Item = txn->get_product_response;
		txn->product_view[0].Price = txn->get_product_response.PriceUsd;

		getRecommendations(txn);
	} else {
		printf("Default Currency is %s. Do convertCurrencyOfProduct\n", defaultCurrency);
		if (txn->productViewCntr != 0) {
			txn->product_view[txn->productViewCntr - 1].Item = txn->get_product_response;
			txn->product_view[txn->productViewCntr - 1].Price = txn->currency_conversion_result;

			getRecommendations(txn);
		}

		int size = 1;
		if (txn->productViewCntr < size) {
			strcpy(txn->currency_conversion_req.ToCode, defaultCurrency);
			strcpy(txn->currency_conversion_req.From.CurrencyCode, txn->get_product_response.PriceUsd.CurrencyCode);
			txn->currency_conversion_req.From.Units = txn->get_product_response.PriceUsd.Units;
			txn->currency_conversion_req.From.Nanos = txn->get_product_response.PriceUsd.Nanos;

			strcpy(txn->rpc_handler, "Convert");
			txn->caller_fn = FRONTEND;
			txn->next_fn = CURRENCY_SVC;

			txn->productViewCntr++;
		}
	}
	return;
}

void insertCart(struct http_transaction *txn) {
	char *query = httpQueryParser(txn->request);
	char *req = txn->request;
	AddItemRequest *in = &txn->add_item_request;

	if (strstr(req, "/1/cart?") != NULL && strstr(req, "POST")) {
		// printf("Query : %s\n", query);
		// char *start_of_product_id = strtok(query, "&");
		char *start_of_quantity = strchr(query, '&') + 1;
		in->Item.Quantity = atoi(strchr(start_of_quantity, '=') + 1);
		// strcpy(txn->get_product_request.Id, strchr(start_of_product_id, '=') + 1);
		printf("Product Quantity: %d\n", in->Item.Quantity);
		// product_id=66VCHSJNUP&quantity=1
	} else {
		printf("HTTP Query cannot be parsed!\n");
		printf("\t#### %s\n", query);
		returnResponse(txn); return;
	}

	strcpy(in->UserId, "ucr-students");
	strcpy(in->Item.ProductId, txn->get_product_request.Id);

	strcpy(txn->rpc_handler, "AddItem");
	txn->caller_fn = FRONTEND;
	txn->next_fn = CART_SVC;
	txn->hop_count++;
}

void getShippingQuote(struct http_transaction *txn) {
	GetQuoteRequest* in = &txn->get_quote_request;
	in->num_items = 0;
	txn->get_quote_response.conversion_flag = false;

	int i;
	for (i = 0; i < txn->get_cart_response.num_items; i++) {
		in->Items[i].Quantity = i + 1;
		in->num_items++;
	}

	strcpy(txn->rpc_handler, "GetQuote");
	txn->caller_fn = FRONTEND;
	txn->next_fn = SHIPPING_SVC;
	txn->hop_count++;
}

// Convert currency for products in the cart
void convertCurrencyOfCart(struct http_transaction *txn) {
	if (strcmp(defaultCurrency, "USD") == 0) {
		printf("Default Currency is USD. Skip convertCurrencyOfCart\n");
		calculateTotalPrice(txn); return;
	} else {
		if (txn->cartItemCurConvertCntr != 0) {
			txn->cart_item_view[txn->cartItemCurConvertCntr - 1].Price = txn->currency_conversion_result;
		}

		if (txn->cartItemCurConvertCntr < txn->cartItemViewCntr) {
			printf("Default Currency is %s. Do convertCurrencyOfCart\n", defaultCurrency);
			strcpy(txn->currency_conversion_req.ToCode, defaultCurrency);
			txn->currency_conversion_req.From = txn->cart_item_view[txn->cartItemCurConvertCntr].Price;

			strcpy(txn->rpc_handler, "Convert");
			txn->caller_fn = FRONTEND;
			txn->next_fn = CURRENCY_SVC;

			txn->cartItemCurConvertCntr++;
			return;
		} else {
			calculateTotalPrice(txn); return;
		}
	}
}

void getCartItemInfo(struct http_transaction *txn) {
	printf("[%s()] %d items in the cart.\n", __func__, txn->get_cart_response.num_items);
	if (txn->get_cart_response.num_items <= 0) {
		printf("[%s()] None items in the cart.\n", __func__);
		txn->total_price.Units = 0;
		txn->total_price.Nanos = 0;
		returnResponse(txn); return;
	}

	if (txn->cartItemViewCntr != 0) {
		txn->cart_item_view[txn->cartItemViewCntr - 1].Item = txn->get_product_response;
		txn->cart_item_view[txn->cartItemViewCntr - 1].Quantity = txn->get_cart_response.Items[txn->cartItemViewCntr - 1].Quantity;
		txn->cart_item_view[txn->cartItemViewCntr - 1].Price = txn->get_product_response.PriceUsd;
	}

	if (txn->cartItemViewCntr < txn->get_cart_response.num_items) {
		strcpy(txn->get_product_request.Id, txn->get_cart_response.Items[txn->cartItemViewCntr].ProductId);
		// printf("Product ID: %s\n", txn->get_product_request.Id);

		strcpy(txn->rpc_handler, "GetProduct");
		txn->caller_fn = FRONTEND;
		txn->next_fn = PRODUCTCATA_SVC;

		txn->cartItemViewCntr++;
	} else {
		txn->cartItemCurConvertCntr = 0;
		convertCurrencyOfCart(txn);
		txn->hop_count++;
	}
}

// Convert currency for a ShippingQuote
void convertCurrencyOfShippingQuote(struct http_transaction *txn) {
	if (strcmp(defaultCurrency, "USD") == 0) {
		printf("[%s()] Default Currency is USD. Skip convertCurrencyOfShippingQuote\n", __func__);
		txn->get_quote_response.conversion_flag = true;

	} else {
		if (txn->get_quote_response.conversion_flag == true) {
			txn->get_quote_response.CostUsd = txn->currency_conversion_result;
			printf("[%s()] Write back convertCurrencyOfShippingQuote\n", __func__);

		} else {
			printf("[%s()] Default Currency is %s. Do convertCurrencyOfShippingQuote\n", __func__, defaultCurrency);
			strcpy(txn->currency_conversion_req.ToCode, defaultCurrency);
			strcpy(txn->currency_conversion_req.From.CurrencyCode, txn->get_quote_response.CostUsd.CurrencyCode);
			txn->currency_conversion_req.From.Units = txn->get_quote_response.CostUsd.Units;
			txn->currency_conversion_req.From.Nanos = txn->get_quote_response.CostUsd.Nanos;

			strcpy(txn->rpc_handler, "Convert");
			txn->caller_fn = FRONTEND;
			txn->next_fn = CURRENCY_SVC;
		}
	}
	return;
}

void calculateTotalPrice(struct http_transaction *txn) {
	printf("[%s()] Calculating total price...\n", __func__);
	int i = 0;
	for (i = 0; i < txn->cartItemViewCntr; i++) {
		MultiplySlow(&txn->cart_item_view[i].Price, txn->cart_item_view[i].Quantity);
		Sum(&txn->total_price, &txn->cart_item_view[i].Price);
	}
	Sum(&txn->total_price, &txn->get_quote_response.CostUsd);
	returnResponse(txn); return;
}