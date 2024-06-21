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

#include "utility.h"
#include "spright.h"

void set_node(uint8_t fn_id, uint8_t node_idx) {
    cfg->inter_node_rt[fn_id] = node_idx;
}

uint8_t* get_node(uint8_t fn_id) {
    return &cfg->inter_node_rt[fn_id];
}

void delete_node(uint8_t fn_id) {
    cfg->inter_node_rt[fn_id] = 0;
}

void print_ip_address(struct in_addr* ip) {
    printf("%s\n", inet_ntoa(*ip));
}

void print_rt_table() {
    printf("Inter-node Routing Table\n");
    for (int i = 1; i <= cfg->n_nfs; i++) {
        printf("\tFn: %d, Node: %d\n", i, cfg->inter_node_rt[i]);
    }
}

void PrintAdResponse(struct http_transaction *in) {
    int i;
    printf("Ads in AdResponse:\n");
    for(i = 0; i < in->ad_response.num_ads; i++) {
        printf("Ad[%d] RedirectUrl: %s\tText: %s\n", i + 1, in->ad_response.Ads[i].RedirectUrl, in->ad_response.Ads[i].Text);
    }
    printf("\n");
}

void PrintSupportedCurrencies (struct http_transaction *in) {
    printf("Supported Currencies: ");
    int i = 0;
    for (i = 0; i < in->get_supported_currencies_response.num_currencies; i++) {
        printf("%d. %s\t", i + 1, in->get_supported_currencies_response.CurrencyCodes[i]);
    }
    printf("\n");
}

void PrintProduct(Product *p) {
    printf("Product Name: %s\t ID: %s\n", p->Name, p->Id);
    printf("Product Description: %s\n", p->Description);
    printf("Product Picture: %s\n", p->Picture);
    printf("Product Price: %s %ld.%d\n", p->PriceUsd.CurrencyCode, p->PriceUsd.Units, p->PriceUsd.Nanos);
    printf("Product Categories: ");

    int i = 0;
    for (i = 0; i < p->num_categories; i++ ) {
        printf("%d. %s\t", i + 1, p->Categories[i]);
    }
    printf("\n\n");
}

void PrintListProductsResponse(struct http_transaction *txn) {
    printf("### PrintListProductsResponse ###\n");
    ListProductsResponse* out = &txn->list_products_response;
    int size = sizeof(out->Products)/sizeof(out->Products[0]);
    int i = 0;
    for (i = 0; i < size; i++) {
        PrintProduct(&out->Products[i]);
    }
    return;
}

void PrintGetProductResponse(struct http_transaction *txn) {
    printf("### PrintGetProductResponse ###\n");
    PrintProduct(&txn->get_product_response);
}

void PrintSearchProductsResponse(struct http_transaction *txn) {
    printf("### PrintSearchProductsResponse ###\n");
    SearchProductsResponse* out = &txn->search_products_response;
    int i;
    for (i = 0; i < out->num_products; i++) {
        PrintProduct(&out->Results[i]);
    }
    return;
}

void PrintGetCartResponse(struct http_transaction *txn) {
    printf("\t\t#### PrintGetCartResponse ####\n");
    Cart *out = &txn->get_cart_response;
    printf("Cart for user %s: \n", out->UserId);

    if (txn->get_cart_response.num_items == -1) {
        printf("EMPTY CART!\n");
        return;
    }

    int i;
    for (i = 0; i < out->num_items; i++) {
        printf("\t%d. ProductId: %s \tQuantity: %d\n", i + 1, out->Items[i].ProductId, out->Items[i].Quantity);
    }
    printf("\n");
    return;
}

void PrintConversionResult(struct http_transaction *in) {
    printf("Conversion result: ");
    printf("CurrencyCode: %s\t", in->currency_conversion_result.CurrencyCode);
    printf("Value: %ld.%d\n", in->currency_conversion_result.Units, in->currency_conversion_result.Nanos);
}

void MockCurrencyConversionRequest(struct http_transaction *in) {
    strcpy(in->currency_conversion_req.ToCode, "USD");
    strcpy(in->currency_conversion_req.From.CurrencyCode, "EUR");

    in->currency_conversion_req.From.Units = 300;
    in->currency_conversion_req.From.Nanos = 0;
}

void PrintProductView(struct http_transaction *txn) {
    printf("\t\t#### ProductView ####\n");
    
    // int size = sizeof(txn->product_view)/sizeof(txn->product_view[0]);
    int size = txn->productViewCntr;
    int i = 0;
    for (i = 0; i < size; i++) {
        Product *p = &txn->product_view[i].Item;
        Money *m = &txn->product_view[i].Price;
        printf("Product Name: %s\t ID: %s\n", p->Name, p->Id);
        printf("Product %s Price:  %ld.%d\n", p->PriceUsd.CurrencyCode, p->PriceUsd.Units, p->PriceUsd.Nanos);
        printf("Product %s Price:  %ld.%d\n\n", m->CurrencyCode, m->Units, m->Nanos);
    }
}

void PrintListRecommendationsResponse(struct http_transaction *txn) {
    printf("Recommended Product ID: %s\n", txn->list_recommendations_response.ProductId);
}

void PrintShipOrderResponse(struct http_transaction *txn) {
    ShipOrderResponse *out = &txn->ship_order_response;
    printf("Tracking ID: %s\n", out->TrackingId);
}

void PrintGetQuoteResponse(struct http_transaction *txn) {
    GetQuoteResponse* out = &txn->get_quote_response;
    printf("Shipping cost: %s %ld.%d\n", out->CostUsd.CurrencyCode, out->CostUsd.Units, out->CostUsd.Nanos);
}

void PrintTotalPrice(struct http_transaction *txn) {
    printf("Total Price:  %ld.%d\n", txn->total_price.Units, txn->total_price.Nanos);
}

void Sum(Money *total, Money *add) {

    total->Units = total->Units + add->Units;
    total->Nanos = total->Nanos + add->Nanos;

    if ((total->Units == 0 && total->Nanos == 0) || (total->Units > 0 && total->Nanos >= 0) || (total->Units < 0 && total->Nanos <= 0)) {
        // same sign <units, nanos>
        total->Units += (int64_t)(total->Nanos / NANOSMOD);
        total->Nanos = total->Nanos % NANOSMOD;
    } else {
        // different sign. nanos guaranteed to not to go over the limit
        if (total->Units > 0) {
            total->Units--;
            total->Nanos += NANOSMOD;
        } else {
            total->Units++;
            total->Nanos -= NANOSMOD;
        }
    }

    return;
}

void MultiplySlow(Money *total, uint32_t n) {
    for (; n > 1 ;) {
        Sum(total, total);
        n--;
    }
    return;
}

void PrintPlaceOrderRequest(struct http_transaction *txn) {
    printf("[%s()] email: %s\n", __func__, txn->place_order_request.Email);
    printf("[%s()] street_address: %s\n", __func__, txn->place_order_request.address.StreetAddress);
    printf("[%s()] zip_code: %d\n", __func__, txn->place_order_request.address.ZipCode);
    printf("[%s()] city: %s\n", __func__, txn->place_order_request.address.City);;
    printf("[%s()] state: %s\n", __func__, txn->place_order_request.address.State);
    printf("[%s()] country: %s\n", __func__, txn->place_order_request.address.Country);
    printf("[%s()] credit_card_number: %s\n", __func__, txn->place_order_request.CreditCard.CreditCardNumber);
    printf("[%s()] credit_card_expiration_month: %d\n", __func__, txn->place_order_request.CreditCard.CreditCardExpirationMonth);
    printf("[%s()] credit_card_expiration_year: %d\n", __func__, txn->place_order_request.CreditCard.CreditCardExpirationYear);
    printf("[%s()] credit_card_cvv: %d\n\n", __func__, txn->place_order_request.CreditCard.CreditCardCvv);
}

void parsePlaceOrderRequest(struct http_transaction *txn) {
    char *query = httpQueryParser(txn->request);
    // printf("QUERY: %s\n", query);

    char *start_of_query = strtok(query, "&");
    // char *email = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.Email, strchr(start_of_query, '=') + 1);
    // printf("[%s()] email: %s\n", __func__, txn->place_order_request.Email);

    start_of_query = strtok(NULL, "&");
    // char *street_address = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.StreetAddress, strchr(start_of_query, '=') + 1);
    // printf("[%s()] street_address: %s\n", __func__, txn->place_order_request.address.StreetAddress);

    start_of_query = strtok(NULL, "&");
    // char *zip_code = strchr(start_of_query, '=') + 1;
    txn->place_order_request.address.ZipCode = atoi(strchr(start_of_query, '=') + 1);
    // printf("[%s()] zip_code: %d\n", __func__, txn->place_order_request.address.ZipCode);

    start_of_query = strtok(NULL, "&");
    // char *city = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.City, strchr(start_of_query, '=') + 1);
    // printf("[%s()] city: %s\n", __func__, txn->place_order_request.address.City);

    start_of_query = strtok(NULL, "&");
    // char *state = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.State, strchr(start_of_query, '=') + 1);
    // printf("[%s()] state: %s\n", __func__, txn->place_order_request.address.State);

    start_of_query = strtok(NULL, "&");
    // char *country = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.Country, strchr(start_of_query, '=') + 1);
    // printf("[%s()] country: %s\n", __func__, txn->place_order_request.address.Country);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_number = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.CreditCard.CreditCardNumber, strchr(start_of_query, '=') + 1);
    // printf("[%s()] credit_card_number: %s\n", __func__, txn->place_order_request.CreditCard.CreditCardNumber);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_expiration_month = strchr(start_of_query, '=') + 1;
    txn->place_order_request.CreditCard.CreditCardExpirationMonth = atoi(strchr(start_of_query, '=') + 1);
    // printf("[%s()] credit_card_expiration_month: %d\n", __func__, txn->place_order_request.CreditCard.CreditCardExpirationMonth);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_expiration_year = strchr(start_of_query, '=') + 1;
    txn->place_order_request.CreditCard.CreditCardExpirationYear = atoi(strchr(start_of_query, '=') + 1);
    // printf("[%s()] credit_card_expiration_year: %d\n", __func__, txn->place_order_request.CreditCard.CreditCardExpirationYear);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_cvv = strchr(start_of_query, '=') + 1;
    txn->place_order_request.CreditCard.CreditCardCvv = atoi(strchr(start_of_query, '=') + 1);
    // printf("[%s()] credit_card_cvv: %d\n\n", __func__, txn->place_order_request.CreditCard.CreditCardCvv);
}

char* httpQueryParser(char* req) {
    char tmp[600]; strcpy(tmp, req);

    char *start_of_path = strtok(tmp, " ");
    start_of_path = strtok(NULL, " ");
       // printf("%s\n", start_of_path); //printing the token
    char *start_of_query = strchr(start_of_path, '?') + 1;
    // printf("%s\n", start_of_query); //product_id=66VCHSJNUP&quantity=1

    return start_of_query;
}