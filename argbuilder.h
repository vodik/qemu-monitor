#pragma once

#include <stdlib.h>
#include "util.h"

typedef struct buffer {
    char *data;
    size_t len;
    size_t buflen;

    size_t *idx;
    size_t idx_len;
    size_t idx_buflen;
} args_t;

int args_init(args_t *buf, size_t reserve);
void args_clear(args_t *buf);
int args_newarg(args_t *buf);
ssize_t args_printf(args_t *buf, const char *fmt, ...) _printf_(2,3);

size_t args_build_argv(args_t *buf, char ***argv);
