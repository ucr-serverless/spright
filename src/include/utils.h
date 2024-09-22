/*
 * Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#ifndef COMMON_UTILS_H_
#define COMMON_UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <doca_error.h>
#include <doca_types.h>

#ifndef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y)) /* Return the minimum value between X and Y */
#endif

#ifndef MAX
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y)) /* Return the maximum value between X and Y */
#endif

/*
 * Prints DOCA SDK and runtime versions
 *
 * @param [in]: unused
 * @doca_config [in]: unused
 * @return: the function exit with EXIT_SUCCESS
 */
doca_error_t sdk_version_callback(void *param, void *doca_config);

/*
 * Read the entire content of a file into a buffer
 *
 * @path [in]: file path
 * @out_bytes [out]: file data buffer
 * @out_bytes_len [out]: file length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t read_file(char const *path, char **out_bytes, size_t *out_bytes_len);

/*
 * Init a uint16_t array with linear number start from zero
 *
 * @array [in]: pointer to array to init
 * @n [in]: number of element to init
 */
void linear_array_init_u16(uint16_t *array, uint16_t n);

#ifdef DOCA_USE_LIBBSD

#include <bsd/string.h>

#else

#ifndef strlcpy

/*
 * This method wraps our implementation of strlcpy when libbsd is missing
 * @dst [in]: destination string
 * @src [in]: source string
 * @size [in]: size, in bytes, of the destination buffer
 * @return: total length of the string (src) we tried to create
 */
size_t strlcpy(char *dst, const char *src, size_t size);

#endif /* strlcpy */

#ifndef strlcat

/*
 * This method wraps our implementation of strlcat when libbsd is missing
 * @dst [in]: destination string
 * @src [in]: source string
 * @size [in]: size, in bytes, of the destination buffer
 * @return: total length of the string (src) we tried to create
 */
size_t strlcat(char *dst, const char *src, size_t size);

#endif /* strlcat */

#endif /* DOCA_USE_LIBBSD */

#endif /* COMMON_UTILS_H_ */
