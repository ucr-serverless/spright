/*
 * Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <string.h>

#include "pack.h"

uint64_t ntohq(uint64_t value)
{
    const int numeric_one = 1;

    /* If we are in a Big-Endian architecture, we don't need to do anything */
    if (*(const uint8_t *)&numeric_one != 1)
        return value;

    /* Swap the 8 bytes of our value */
    value = SET_BYTE((uint64_t)GET_BYTE(value, 0), 7) | SET_BYTE((uint64_t)GET_BYTE(value, 1), 6) |
        SET_BYTE((uint64_t)GET_BYTE(value, 2), 5) | SET_BYTE((uint64_t)GET_BYTE(value, 3), 4) |
        SET_BYTE((uint64_t)GET_BYTE(value, 4), 3) | SET_BYTE((uint64_t)GET_BYTE(value, 5), 2) |
        SET_BYTE((uint64_t)GET_BYTE(value, 6), 1) | SET_BYTE((uint64_t)GET_BYTE(value, 7), 0);

    return value;
}

void pack_uint8(uint8_t **buffer, uint8_t value)
{
    uint8_t *write_head = *buffer;

    *write_head++ = value;
    *buffer = write_head;
}

void pack_uint16(uint8_t **buffer, uint16_t value)
{
    uint8_t *write_head = *buffer;

    *write_head++ = GET_BYTE(value, 1);
    *write_head++ = GET_BYTE(value, 0);
    *buffer = write_head;
}

void pack_uint32(uint8_t **buffer, uint32_t value)
{
    uint8_t *write_head = *buffer;

    *write_head++ = GET_BYTE(value, 3);
    *write_head++ = GET_BYTE(value, 2);
    *write_head++ = GET_BYTE(value, 1);
    *write_head++ = GET_BYTE(value, 0);
    *buffer = write_head;
}

void pack_uint64(uint8_t **buffer, uint64_t value)
{
    uint8_t *write_head = *buffer;

    *write_head++ = GET_BYTE(value, 7);
    *write_head++ = GET_BYTE(value, 6);
    *write_head++ = GET_BYTE(value, 5);
    *write_head++ = GET_BYTE(value, 4);
    *write_head++ = GET_BYTE(value, 3);
    *write_head++ = GET_BYTE(value, 2);
    *write_head++ = GET_BYTE(value, 1);
    *write_head++ = GET_BYTE(value, 0);
    *buffer = write_head;
}

void pack_blob(uint8_t **buffer, size_t length, uint8_t *object)
{
    uint8_t *write_head = *buffer;

    memcpy(write_head, object, length);
    write_head += length;
    *buffer = write_head;
}

uint8_t unpack_uint8(uint8_t **buffer)
{
    uint8_t value = **buffer;

    *buffer += 1;

    return value;
}

uint16_t unpack_uint16(uint8_t **buffer)
{
    uint16_t value = 0;
    uint8_t *read_head = *buffer;

    value |= SET_BYTE(*read_head++, 1);
    value |= SET_BYTE(*read_head++, 0);
    *buffer = read_head;

    return value;
}

uint32_t unpack_uint32(uint8_t **buffer)
{
    uint32_t value = 0;
    uint8_t *read_head = *buffer;

    value |= SET_BYTE(*read_head++, 3);
    value |= SET_BYTE(*read_head++, 2);
    value |= SET_BYTE(*read_head++, 1);
    value |= SET_BYTE(*read_head++, 0);
    *buffer = read_head;

    return value;
}

uint64_t unpack_uint64(uint8_t **buffer)
{
    uint64_t value = 0;
    uint8_t *read_head = *buffer;

    value |= SET_BYTE((uint64_t)(*read_head++), 7);
    value |= SET_BYTE((uint64_t)(*read_head++), 6);
    value |= SET_BYTE((uint64_t)(*read_head++), 5);
    value |= SET_BYTE((uint64_t)(*read_head++), 4);
    value |= SET_BYTE((uint64_t)(*read_head++), 3);
    value |= SET_BYTE((uint64_t)(*read_head++), 2);
    value |= SET_BYTE((uint64_t)(*read_head++), 1);
    value |= SET_BYTE((uint64_t)(*read_head++), 0);
    *buffer = read_head;

    return value;
}

void unpack_blob(uint8_t **buffer, size_t length, uint8_t *output)
{
    uint8_t *read_head = *buffer;

    memcpy(output, read_head, length);
    read_head += length;
    *buffer = read_head;
}
