#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define WHITESPACE " \t"

static size_t skip_whitespace(const char *s)
{
    return strspn(s, WHITESPACE);
}

static char *strstripped(const char *s, size_t length)
{
    size_t skipped = skip_whitespace(s);
    char *p, *new = strndup(&s[skipped], length - skipped);

    for (p = &new[length - skipped]; p > s; --p) {
        if (!strchr(WHITESPACE, p[-1]))
            break;
    }

    *p = 0;
    return new;
}

static void split_key_value(const char *line, char **key, char **value)
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
