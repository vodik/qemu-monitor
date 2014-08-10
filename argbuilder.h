#pragma once

#include <stdlib.h>

typedef struct buffer {
    char *data;
    size_t len;
    size_t buflen;

    size_t *idx;
    size_t idx_len;
    size_t idx_buflen;
} buffer_t;

int buffer_init(buffer_t *buf, size_t reserve);
void buffer_clear(buffer_t *buf);
int buffer_newarg(buffer_t *buf);
ssize_t buffer_printf(buffer_t *buf, const char *fmt, ...);

size_t buffer_build_argv(buffer_t *buf, char ***argv);
