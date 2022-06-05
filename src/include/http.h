/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <rte_eal.h>

#define HTTP_MSG_LENGTH_HEADER_MAX (1U << 12)
#define HTTP_MSG_LENGTH_BODY_MAX (1U << 16)
#define HTTP_MSG_LENGTH_MAX (HTTP_MSG_LENGTH_HEADER_MAX + \
                             HTTP_MSG_LENGTH_BODY_MAX)

#define GATEWAY 		(uint8_t)0
#define FRONTEND 		(uint8_t)1
#define CURRENCY_SVC 	(uint8_t)2
#define PRODUCTCATA_SVC (uint8_t)3
#define CART_SVC		(uint8_t)4
#define RECOMMEND_SVC	(uint8_t)5
#define SHIPPING_SVC	(uint8_t)6
#define CHECKOUT_SVC	(uint8_t)7
#define PAYMENT_SVC		(uint8_t)8
#define EMAIL_SVC		(uint8_t)9
#define AD_SVC			(uint8_t)10

typedef struct _money {
	char CurrencyCode[10];
	int64_t Units;
	int32_t Nanos;
} Money;

typedef struct _address {
	char StreetAddress[50];
	char City[15];
	char State[15];
	char Country[15];
	int32_t ZipCode;
} Address;

typedef struct _cartItem {
	char ProductId[50];
	int32_t Quantity;
} CartItem;

typedef struct _orderItem {
	CartItem Item;
	Money Cost;
} OrderItem;

typedef struct _orderResult {
	char OrderId[40];
	char ShippingTrackingId[100];
	Money ShippingCost;
	Address ShippingAddress;
	OrderItem Items[10];
} OrderResult;

typedef struct _cart {
	char UserId[50];
	int num_items;
	CartItem Items[10];
} Cart;

// typedef struct _addItemResponse{
// 	bool return_code; // 1 - success
// } AddItemResponse;

typedef struct _addItemRequest{
	char UserId[50];
	CartItem Item;
} AddItemRequest;

typedef struct _emptyCartRequest{
	char UserId[50];
} EmptyCartRequest;

typedef struct _getCartRequest {
	char UserId[50];
} GetCartRequest;

typedef struct _product {
	char Id[20];
	char Name[30];
	char Description[100];
	char Picture[60];
	Money PriceUsd;
	// Categories such as "clothing" or "kitchen" that can be used to look up
	// other related products.
	int num_categories;
	char Categories[10][20];
} Product;

typedef struct _productView {
	Product Item;
	Money Price;
} productView;

typedef struct _cartItemView {
	Product Item;
	int32_t Quantity;
	Money Price;
} cartItemView;

typedef struct _listProductsResponse {
	int num_products;
	Product Products[9];
} ListProductsResponse;

typedef struct _searchProductsResponse {
	int num_products;
	Product Results[9];
} SearchProductsResponse;

typedef struct _getProductRequest{
	char Id[20];
} GetProductRequest;

typedef struct _searchProductsRequest {
	char Query[50];
} SearchProductsRequest;

typedef struct _listRecommendationsRequest {
	// char UserId[50];
	char ProductId[20];
} ListRecommendationsRequest;

typedef struct _listRecommendationsResponse{
	char ProductId [20];
} ListRecommendationsResponse;

typedef struct _shipOrderRequest {
	Address address;
	CartItem Items[10];
} ShipOrderRequest;

typedef struct _shipOrderResponse{
	char TrackingId[100];
} ShipOrderResponse;

typedef struct _getQuoteRequest{
	Address address;
	int num_items;
	CartItem Items[10];
} GetQuoteRequest;

typedef struct _sendOrderConfirmationRequest {
	char Email[50];
	// OrderResult Order;
} SendOrderConfirmationRequest;

typedef struct _getQuoteResponse {
	bool conversion_flag;
	Money CostUsd;
} GetQuoteResponse;

typedef struct _creditCardInfo {
	char CreditCardNumber[30];
	int32_t CreditCardCvv;
	int32_t CreditCardExpirationYear;
	int32_t CreditCardExpirationMonth;
} CreditCardInfo;

typedef struct _orderPrep {
	OrderItem orderItems[10];
	CartItem cartItems[10];
	Money shippingCostLocalized;
} orderPrep;

typedef struct _placeOrderRequest{
	char UserId[50];
	char UserCurrency[5];
	Address address;
	char Email[50];
	CreditCardInfo CreditCard;
} PlaceOrderRequest;

typedef struct _chargeRequest {
	Money Amount;
	CreditCardInfo CreditCard;
} ChargeRequest;

typedef struct _chargeResponse {
	char TransactionId[40];
} ChargeResponse;

typedef struct _getSupportedCurrenciesResponse {
	int num_currencies;
	char CurrencyCodes[6][10];
} GetSupportedCurrenciesResponse;

typedef struct _currencyConversionRequest {
	Money From;
	char ToCode[10];
} CurrencyConversionRequest;

typedef struct _ad {
   char RedirectUrl[100];   
   char Text[100];
} Ad;

typedef struct _adrequest {
	int num_context_keys;
	char ContextKeys[10][100];
} AdRequest;

typedef struct _adresponse {
	int num_ads;
	Ad Ads[10];
} AdResponse;

struct http_transaction {
	int sockfd;
	uint8_t route_id;
	uint8_t next_fn;
	uint8_t hop_count;
	uint8_t caller_fn;

	uint32_t length_request;
	uint32_t length_response;

	char rpc_handler[64];
	char caller_nf[64];
	char request[HTTP_MSG_LENGTH_MAX];
	char response[HTTP_MSG_LENGTH_MAX];

	AdRequest ad_request;
	AdResponse ad_response;

	CurrencyConversionRequest currency_conversion_req;
	Money currency_conversion_result;
	GetSupportedCurrenciesResponse get_supported_currencies_response;

	SendOrderConfirmationRequest email_req;

	ChargeRequest charge_request;
	ChargeResponse charge_response;

	GetQuoteRequest get_quote_request;
	GetQuoteResponse get_quote_response;
	ShipOrderRequest ship_order_request;
	ShipOrderResponse ship_order_response;

	SearchProductsRequest search_products_request;
	SearchProductsResponse search_products_response;
	GetProductRequest get_product_request;
	Product get_product_response;
	ListProductsResponse list_products_response;

	EmptyCartRequest empty_cart_request;
	AddItemRequest add_item_request;
	// bool add_item_response;
	GetCartRequest get_cart_request;
	Cart get_cart_response;

	ListRecommendationsRequest list_recommendations_request;
	ListRecommendationsResponse list_recommendations_response;

	productView product_view[9];
	int productViewCntr;

	cartItemView cart_item_view[10];
	int cartItemViewCntr;
	int cartItemCurConvertCntr;
	Money total_price;

	PlaceOrderRequest place_order_request;
	orderPrep order_prep;
	OrderResult order_result;
	OrderItem order_item_view[10];
	int orderItemViewCntr;
	int orderItemCurConvertCntr;
	uint8_t checkoutsvc_hop_cnt;
};

#endif /* HTTP_H */
