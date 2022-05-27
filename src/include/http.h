/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 University of California, Riverside
 */

#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

#define HTTP_MSG_LENGTH_HEADER_MAX (1U << 12)
#define HTTP_MSG_LENGTH_BODY_MAX (1U << 16)
#define HTTP_MSG_LENGTH_MAX (HTTP_MSG_LENGTH_HEADER_MAX + \
                             HTTP_MSG_LENGTH_BODY_MAX)

struct http_transaction {
	int sockfd;
	uint8_t route_id;
	uint8_t hop_count;

	uint32_t length_request;
	uint32_t length_response;

	char rpc_handler[64];
	char caller_nf[64];
	char request[HTTP_MSG_LENGTH_MAX];
	char response[HTTP_MSG_LENGTH_MAX];
};

#endif /* HTTP_H */
