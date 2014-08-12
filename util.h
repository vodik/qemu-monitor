#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <unistd.h>

#define _unlikely_(x)       __builtin_expect(!!(x), 1)
#define _unused_            __attribute__((unsused))
#define _noreturn_          __attribute__((noreturn))
#define _cleanup_(x)        __attribute__((cleanup(x)))
#define _printf_(a,b)       __attribute__((format (printf, a, b)))
#define _cleanup_free_      _cleanup_(freep)
#define _cleanup_close_     _cleanup_(closep)

static inline void freep(void *p) { free(*(void **)p); }
static inline void closep(int *fd) { if (*fd >= 0) close(*fd); }

static inline void *zero(void *s, size_t n) { return memset(s, 0, n); }
static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }

void hex_dump(const char *desc, const void *addr, size_t len);
