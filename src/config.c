#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "util.h"

#define WHITESPACE " \t\r\n"

static char *strstripped(const char *s, size_t length)
{
    char *new = NULL;
    size_t idx, skipped = strspn(s, WHITESPACE);
    length -= skipped;

    if (length > 0) {
        new = strndup(&s[skipped], length);

        for (idx = length; idx != 0; --idx) {
            if (!strchr(WHITESPACE, new[idx]))
                break;
        }

        new[idx + 1] = 0;
    }

    return new;
}

void split_key_value(const char *line, char **key, char **value)
{
    size_t length = strcspn(line, "#");
    if (length == 0) {
        *key = *value = NULL;
        return;
    }

    const char *sep = memchr(line, '=', length);

    *key = strstripped(line, sep - line);
    *value = strstripped(sep + 1, length - (sep - line) - 1);
}
