/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Simon Gomizelj, 2014
 */

#include "argbuilder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
/* #include "util.h" */

#define MONITOR_SOCK "/run/user/1000/monitor"
#define _unlikely_(x) __builtin_expect(!!(x), 1)
static inline void *zero(void *s, size_t n) { return memset(s, 0, n); }

static void hex_dump(const char *desc, const void *addr, size_t len)
{
    const unsigned char *pc = addr;
    unsigned char readable[17] = { 0 };
    size_t i;

    if (desc)
        printf("%s:\n", desc);

    for (i = 0; i < len; i++) {
        size_t mod16 = i % 16;

        if (mod16 == 0) {
            if (readable[0])
                printf("  %s\n", readable);
            printf(" %06zx:", i);
        }

        printf(i % 2 ? "%02x" : " %02x", pc[i]);

        if (pc[i] < 0x20 || pc[i] > 0x7e)
            readable[mod16] = '.';
        else
            readable[mod16] = pc[i];
        readable[mod16 + 1] = '\0';
    }

    for (; i % 16 != 0; ++i)
        printf(i % 2 ? "  " : "   ");
    printf("  %s\n", readable);
}

static inline size_t next_power(size_t x)
{
    return 1UL << (64 - __builtin_clzl(x - 1));
}

static int buffer_extendby(buffer_t *buf, size_t extby)
{
    char *data;
    size_t newlen = _unlikely_(!buf->buflen && extby < 64)
        ? 64 : buf->len + extby;

    if (newlen > buf->buflen) {
        newlen = next_power(newlen);
        data = realloc(buf->data, newlen);
        if (!data)
            return -errno;

        buf->buflen = newlen;
        buf->data = data;
    }

    return 0;
}

static int buffer_store_idx(buffer_t *buf)
{
    size_t newlen = _unlikely_(!buf->idx_buflen) ? 4 : buf->idx_len + 1;
    size_t *idx;

    if (newlen > buf->idx_buflen) {
        newlen = next_power(newlen);
        idx = realloc(buf->idx, newlen * sizeof(size_t));
        if (!idx)
            return -errno;

        buf->idx_buflen = newlen;
        buf->idx = idx;
    }

    buf->idx[buf->idx_len] = buf->len;
    buf->idx_len += 1;
    return 0;
}

int buffer_init(buffer_t *buf, size_t reserve)
{
    zero(buf, sizeof(buffer_t));

    if (_unlikely_(reserve && buffer_extendby(buf, reserve) < 0))
        return -errno;

    buf->data[buf->len] = '\0';
    return 0;
}

void buffer_clear(buffer_t *buf)
{
    buf->len = 0;
    if (buf->data)
        buf->data[buf->len] = '\0';
}

int buffer_newarg(buffer_t *buf)
{
    if (_unlikely_(buffer_extendby(buf, 2) < 0))
        return -errno;

    buf->data[buf->len++] = '\0';
    buf->data[buf->len] = '\0';

    buffer_store_idx(buf);
    return 0;
}

ssize_t buffer_printf(buffer_t *buf, const char *fmt, ...)
{
    if (buffer_newarg(buf) < 0)
        return -errno;

    size_t len = buf->buflen - buf->len;
    char *p = &buf->data[buf->len];

    va_list ap;
    va_start(ap, fmt);
    size_t rc = vsnprintf(p, len, fmt, ap);
    va_end(ap);

    if (_unlikely_(rc >= len)) {
        if (_unlikely_(buffer_extendby(buf, rc + 1) < 0))
            return -errno;

        p = &buf->data[buf->len];

        va_start(ap, fmt);
        rc = vsnprintf(p, rc + 1, fmt, ap);
        va_end(ap);
    }

    buf->len += rc;

    return 0;
}

size_t buffer_build_argv(buffer_t *buf, char ***_argv)
{
    size_t idx, len = buf->idx_len + 1;
    char **argv = malloc(sizeof(char *) * len);

    for (idx = 0; idx < buf->idx_len; ++idx)
        argv[idx] = &buf->data[buf->idx[idx]];

    argv[idx] = NULL;
    *_argv = argv;
    return len;
}
