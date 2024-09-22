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

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdnoreturn.h>

#include <doca_version.h>
#include <doca_log.h>

#include "utils.h"

DOCA_LOG_REGISTER(UTILS)

noreturn doca_error_t sdk_version_callback(void *param, void *doca_config)
{
    (void)(param);
    (void)(doca_config);

    printf("DOCA SDK     Version (Compilation): %s\n", doca_version());
    printf("DOCA Runtime Version (Runtime):     %s\n", doca_version_runtime());
    /* We assume that when printing DOCA's versions there is no need to continue the program's execution */
    exit(EXIT_SUCCESS);
}

doca_error_t read_file(char const *path, char **out_bytes, size_t *out_bytes_len)
{
    FILE *file;
    char *bytes;

    file = fopen(path, "rb");
    if (file == NULL)
        return DOCA_ERROR_NOT_FOUND;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return DOCA_ERROR_IO_FAILED;
    }

    long const nb_file_bytes = ftell(file);

    if (nb_file_bytes == -1) {
        fclose(file);
        return DOCA_ERROR_IO_FAILED;
    }

    if (nb_file_bytes == 0) {
        fclose(file);
        return DOCA_ERROR_INVALID_VALUE;
    }

    bytes = malloc(nb_file_bytes);
    if (bytes == NULL) {
        fclose(file);
        return DOCA_ERROR_NO_MEMORY;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        free(bytes);
        fclose(file);
        return DOCA_ERROR_IO_FAILED;
    }

    size_t const read_byte_count = fread(bytes, 1, nb_file_bytes, file);

    fclose(file);

    if (read_byte_count != (size_t)nb_file_bytes) {
        free(bytes);
        return DOCA_ERROR_IO_FAILED;
    }

    *out_bytes = bytes;
    *out_bytes_len = read_byte_count;

    return DOCA_SUCCESS;
}

void linear_array_init_u16(uint16_t *array, uint16_t n)
{
    uint16_t i;

    for (i = 0; i < n; i++)
        array[i] = i;
}

#ifndef DOCA_USE_LIBBSD

#ifndef strlcpy

#include <string.h>

size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t trimmed_size;
    size_t src_len = strlen(src);

    if (size > 0) {
        trimmed_size = MIN(src_len, (size - 1));

        memcpy(dst, src, trimmed_size);
        dst[trimmed_size] = '\0';
    }

    return src_len;
}

#endif /* strlcpy */

#ifndef strlcat

#include <string.h>

size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t dst_len = strnlen(dst, size);

    if (dst_len >= size)
        return size;

    return dst_len + strlcpy(dst + dst_len, src, size - dst_len);
}

#endif /* strlcat */

#endif /* ! DOCA_USE_LIBBSD */
