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

#include "util.h"

#define MONITOR_SOCK "/run/user/1000/monitor"

static inline size_t next_power(size_t x)
{
    return 1UL << (64 - __builtin_clzl(x - 1));
}

static int args_extendby(args_t *buf, size_t extby)
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

static int args_store_idx(args_t *buf)
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

int args_init(args_t *buf, size_t reserve)
{
    zero(buf, sizeof(args_t));

    if (_unlikely_(reserve && args_extendby(buf, reserve) < 0))
        return -errno;

    buf->data[buf->len] = '\0';
    return 0;
}

void args_clear(args_t *buf)
{
    buf->len = 0;
    if (buf->data)
        buf->data[buf->len] = '\0';
}

int args_newarg(args_t *buf)
{
    if (_unlikely_(args_extendby(buf, 2) < 0))
        return -errno;

    buf->data[buf->len++] = '\0';
    buf->data[buf->len] = '\0';

    args_store_idx(buf);
    return 0;
}

ssize_t args_printf(args_t *buf, const char *fmt, ...)
{
    if (args_newarg(buf) < 0)
        return -errno;

    size_t len = buf->buflen - buf->len;
    char *p = &buf->data[buf->len];

    va_list ap;
    va_start(ap, fmt);
    size_t rc = vsnprintf(p, len, fmt, ap);
    va_end(ap);

    if (_unlikely_(rc >= len)) {
        if (_unlikely_(args_extendby(buf, rc + 1) < 0))
            return -errno;

        p = &buf->data[buf->len];

        va_start(ap, fmt);
        rc = vsnprintf(p, rc + 1, fmt, ap);
        va_end(ap);
    }

    buf->len += rc;

    return 0;
}

size_t args_build_argv(args_t *buf, char ***_argv)
{
    size_t idx, len = buf->idx_len + 1;
    char **argv = malloc(sizeof(char *) * len);

    for (idx = 0; idx < buf->idx_len; ++idx)
        argv[idx] = &buf->data[buf->idx[idx]];

    argv[idx] = NULL;
    *_argv = argv;
    return len;
}
