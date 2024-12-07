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

void set_node(uint8_t fn_id, uint8_t node_idx)
{
    cfg->inter_node_rt[fn_id] = node_idx;
}

uint8_t get_node(uint8_t fn_id)
{
    uint8_t peer_node_idx = cfg->inter_node_rt[fn_id];

    log_debug("Destination function is %u on node %u (%s:%u).", fn_id, peer_node_idx,
              cfg->nodes[peer_node_idx].ip_address, INTERNAL_SERVER_PORT);

    return peer_node_idx;
}

void delete_node(uint8_t fn_id)
{
    cfg->inter_node_rt[fn_id] = 0;
}

void print_ip_address(struct in_addr *ip)
{
    log_info("%s", inet_ntoa(*ip));
}

void print_rt_table()
{
    printf("Inter-node Routing Table\n");
    for (int i = 1; i <= cfg->n_nfs; i++)
    {
        printf("\tFn: %d, Node: %d\n", i, cfg->inter_node_rt[i]);
    }
}

void PrintAdResponse(struct http_transaction *in)
{
    int i;
    log_info("Ads in AdResponse:");
    for (i = 0; i < in->ad_response.num_ads; i++)
    {
        log_info("Ad[%d] RedirectUrl: %s\tText: %s", i + 1, in->ad_response.Ads[i].RedirectUrl,
                 in->ad_response.Ads[i].Text);
    }
    printf("\n");
}

void PrintSupportedCurrencies(struct http_transaction *in)
{
    log_info("Supported Currencies: ");
    int i = 0;
    for (i = 0; i < in->get_supported_currencies_response.num_currencies; i++)
    {
        log_info("%d. %s\t", i + 1, in->get_supported_currencies_response.CurrencyCodes[i]);
    }
    printf("\n");
}

void PrintProduct(Product *p)
{
    log_info("Product Name: %s\t ID: %s", p->Name, p->Id);
    log_info("Product Description: %s", p->Description);
    log_info("Product Picture: %s", p->Picture);
    log_info("Product Price: %s %ld.%d", p->PriceUsd.CurrencyCode, p->PriceUsd.Units, p->PriceUsd.Nanos);
    log_info("Product Categories: ");

    int i = 0;
    for (i = 0; i < p->num_categories; i++)
    {
        log_info("%d. %s\t", i + 1, p->Categories[i]);
    }
    printf("\n");
}

void PrintListProductsResponse(struct http_transaction *txn)
{
    log_info("### PrintListProductsResponse ###");
    ListProductsResponse *out = &txn->list_products_response;
    int size = sizeof(out->Products) / sizeof(out->Products[0]);
    int i = 0;
    for (i = 0; i < size; i++)
    {
        PrintProduct(&out->Products[i]);
    }
    return;
}

void PrintGetProductResponse(struct http_transaction *txn)
{
    log_info("### PrintGetProductResponse ###");
    PrintProduct(&txn->get_product_response);
}

void PrintSearchProductsResponse(struct http_transaction *txn)
{
    log_info("### PrintSearchProductsResponse ###");
    SearchProductsResponse *out = &txn->search_products_response;
    int i;
    for (i = 0; i < out->num_products; i++)
    {
        PrintProduct(&out->Results[i]);
    }
    return;
}

void PrintGetCartResponse(struct http_transaction *txn)
{
    log_info("\t\t#### PrintGetCartResponse ####");
    Cart *out = &txn->get_cart_response;
    log_info("Cart for user %s: ", out->UserId);

    if (txn->get_cart_response.num_items == -1)
    {
        log_info("EMPTY CART!");
        return;
    }

    int i;
    for (i = 0; i < out->num_items; i++)
    {
        log_info("\t%d. ProductId: %s \tQuantity: %d", i + 1, out->Items[i].ProductId, out->Items[i].Quantity);
    }
    printf("\n");
    return;
}

void PrintConversionResult(struct http_transaction *in)
{
    log_info("Conversion result: ");
    log_info("CurrencyCode: %s\t", in->currency_conversion_result.CurrencyCode);
    log_info("Value: %ld.%d", in->currency_conversion_result.Units, in->currency_conversion_result.Nanos);
}

void printMoney(Money *money)
{
    printf("Money:\n");
    printf("  Currency Code: %s\n", money->CurrencyCode);
    printf("  Units: %ld\n", money->Units);
    printf("  Nanos: %d\n", money->Nanos);
}

void printCurrencyConversionRequest(CurrencyConversionRequest *request)
{
    printf("Currency Conversion Request:\n");
    printMoney(&request->From);
    printf("  To Currency Code: %s\n", request->ToCode);
}

void MockCurrencyConversionRequest(struct http_transaction *in)
{
    strcpy(in->currency_conversion_req.ToCode, "USD");
    strcpy(in->currency_conversion_req.From.CurrencyCode, "EUR");

    in->currency_conversion_req.From.Units = 300;
    in->currency_conversion_req.From.Nanos = 0;
}

void PrintProductView(struct http_transaction *txn)
{
    log_info("\t\t#### ProductView ####");

    // int size = sizeof(txn->product_view)/sizeof(txn->product_view[0]);
    int size = txn->productViewCntr;
    int i = 0;
    for (i = 0; i < size; i++)
    {
        Product *p = &txn->product_view[i].Item;
        Money *m = &txn->product_view[i].Price;
        log_info("Product Name: %s\t ID: %s", p->Name, p->Id);
        log_info("Product %s Price:  %ld.%d", p->PriceUsd.CurrencyCode, p->PriceUsd.Units, p->PriceUsd.Nanos);
        log_info("Product %s Price:  %ld.%d", m->CurrencyCode, m->Units, m->Nanos);
    }
}

void PrintListRecommendationsResponse(struct http_transaction *txn)
{
    log_info("Recommended Product ID: %s", txn->list_recommendations_response.ProductId);
}

void PrintShipOrderResponse(struct http_transaction *txn)
{
    ShipOrderResponse *out = &txn->ship_order_response;
    log_info("Tracking ID: %s", out->TrackingId);
}

void PrintGetQuoteResponse(struct http_transaction *txn)
{
    GetQuoteResponse *out = &txn->get_quote_response;
    log_info("Shipping cost: %s %ld.%d", out->CostUsd.CurrencyCode, out->CostUsd.Units, out->CostUsd.Nanos);
}

void PrintTotalPrice(struct http_transaction *txn)
{
    log_info("Total Price:  %ld.%d", txn->total_price.Units, txn->total_price.Nanos);
}

void Sum(Money *total, Money *add)
{

    total->Units = total->Units + add->Units;
    total->Nanos = total->Nanos + add->Nanos;

    if ((total->Units == 0 && total->Nanos == 0) || (total->Units > 0 && total->Nanos >= 0) ||
        (total->Units < 0 && total->Nanos <= 0))
    {
        // same sign <units, nanos>
        total->Units += (int64_t)(total->Nanos / NANOSMOD);
        total->Nanos = total->Nanos % NANOSMOD;
    }
    else
    {
        // different sign. nanos guaranteed to not to go over the limit
        if (total->Units > 0)
        {
            total->Units--;
            total->Nanos += NANOSMOD;
        }
        else
        {
            total->Units++;
            total->Nanos -= NANOSMOD;
        }
    }

    return;
}

void MultiplySlow(Money *total, uint32_t n)
{
    for (; n > 1;)
    {
        Sum(total, total);
        n--;
    }
    return;
}

void PrintPlaceOrderRequest(struct http_transaction *txn)
{
    log_info("email: %s", txn->place_order_request.Email);
    log_info("street_address: %s", txn->place_order_request.address.StreetAddress);
    log_info("zip_code: %d", txn->place_order_request.address.ZipCode);
    log_info("city: %s", txn->place_order_request.address.City);
    ;
    log_info("state: %s", txn->place_order_request.address.State);
    log_info("country: %s", txn->place_order_request.address.Country);
    log_info("credit_card_number: %s", txn->place_order_request.CreditCard.CreditCardNumber);
    log_info("credit_card_expiration_month: %d", txn->place_order_request.CreditCard.CreditCardExpirationMonth);
    log_info("credit_card_expiration_year: %d", txn->place_order_request.CreditCard.CreditCardExpirationYear);
    log_info("credit_card_cvv: %d", txn->place_order_request.CreditCard.CreditCardCvv);
}

void parsePlaceOrderRequest(struct http_transaction *txn)
{
    char *query = httpQueryParser(txn->request);
    // log_info("QUERY: %s", query);

    char *start_of_query = strtok(query, "&");
    // char *email = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.Email, strchr(start_of_query, '=') + 1);
    // log_info("email: %s", txn->place_order_request.Email);

    start_of_query = strtok(NULL, "&");
    // char *street_address = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.StreetAddress, strchr(start_of_query, '=') + 1);
    // log_info("street_address: %s", txn->place_order_request.address.StreetAddress);

    start_of_query = strtok(NULL, "&");
    // char *zip_code = strchr(start_of_query, '=') + 1;
    txn->place_order_request.address.ZipCode = atoi(strchr(start_of_query, '=') + 1);
    // log_info("zip_code: %d", txn->place_order_request.address.ZipCode);

    start_of_query = strtok(NULL, "&");
    // char *city = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.City, strchr(start_of_query, '=') + 1);
    // log_info("city: %s", txn->place_order_request.address.City);

    start_of_query = strtok(NULL, "&");
    // char *state = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.State, strchr(start_of_query, '=') + 1);
    // log_info("state: %s", txn->place_order_request.address.State);

    start_of_query = strtok(NULL, "&");
    // char *country = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.address.Country, strchr(start_of_query, '=') + 1);
    // log_info("country: %s", txn->place_order_request.address.Country);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_number = strchr(start_of_query, '=') + 1;
    strcpy(txn->place_order_request.CreditCard.CreditCardNumber, strchr(start_of_query, '=') + 1);
    // log_info("credit_card_number: %s", txn->place_order_request.CreditCard.CreditCardNumber);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_expiration_month = strchr(start_of_query, '=') + 1;
    txn->place_order_request.CreditCard.CreditCardExpirationMonth = atoi(strchr(start_of_query, '=') + 1);
    // log_info("credit_card_expiration_month: %d", txn->place_order_request.CreditCard.CreditCardExpirationMonth);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_expiration_year = strchr(start_of_query, '=') + 1;
    txn->place_order_request.CreditCard.CreditCardExpirationYear = atoi(strchr(start_of_query, '=') + 1);
    // log_info("credit_card_expiration_year: %d", txn->place_order_request.CreditCard.CreditCardExpirationYear);

    start_of_query = strtok(NULL, "&");
    // char *credit_card_cvv = strchr(start_of_query, '=') + 1;
    txn->place_order_request.CreditCard.CreditCardCvv = atoi(strchr(start_of_query, '=') + 1);
    // log_info("credit_card_cvv: %d", txn->place_order_request.CreditCard.CreditCardCvv);
}

char *httpQueryParser(char *req)
{
    char tmp[600];
    strcpy(tmp, req);

    char *start_of_path = strtok(tmp, " ");
    start_of_path = strtok(NULL, " ");
    char *start_of_query = strchr(start_of_path, '?') + 1;

    // Remove trailing slash if present
    size_t len = strlen(start_of_query);
    if (start_of_query[len - 1] == '/')
    {
        start_of_query[len - 1] = '\0';
    }

    return start_of_query;
}

void debug_http_transaction(struct http_transaction* tx) {
    if (tx == NULL) {
        log_error("Error: NULL http_transaction pointer");
        return;
    }

    log_debug("=== Start HTTP Transaction Debug Info ===");
    log_debug("Basic Information:");
    log_debug("  Tenant ID: %u", tx->tenant_id);
    log_debug("  Socket FD: %d", tx->sockfd);
    log_debug("  Route ID: %u", tx->route_id);
    log_debug("  Next Function: %u", tx->next_fn);
    log_debug("  Hop Count: %u", tx->hop_count);
    log_debug("  Caller Function: %u", tx->caller_fn);

    log_debug("Handlers and Callers:");
    log_debug("  RPC Handler: %s", tx->rpc_handler);
    log_debug("  Caller NF: %s", tx->caller_nf);
    log_debug("=== End HTTP Transaction Debug Info ===");
}