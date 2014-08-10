#include "xdg.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <pwd.h>
#include <sys/types.h>

#include "util.h"

static char *home_dir_cache = NULL;
static char *user_config_dir_cache = NULL;
static char *user_data_dir_cache = NULL;
static char *user_cache_dir_cache = NULL;
static char *user_runtime_dir_cache = NULL;

static char *_printf(const char *fmt, ...)
{
    char *str;
    va_list ap;

    va_start(ap, fmt);
    if (vasprintf(&str, fmt, ap) < 0) {
        va_end(ap);
        return NULL;
    }
    va_end(ap);
    return str;
}

const char *get_home_dir(void)
{
    if (home_dir_cache)
        return home_dir_cache;

    char *home = getenv("HOME");
    if (home && home[0]) {
        home_dir_cache = strdup(home);
    } else {
        struct passwd *pwd = getpwuid(getuid());
        if (!pwd)
            err(EXIT_FAILURE, "failed to get pwd entry for user");
        home_dir_cache = strdup(pwd->pw_dir);
    }

    return home_dir_cache;
}

static char *get_xdg_dir(const char *env, char *default_path)
{
    char *xdg_dir = getenv(env);

    if (xdg_dir && xdg_dir[0])
        return strdup(xdg_dir);
    else if (default_path)
        return _printf("%s/%s", get_home_dir(), default_path);
    else
        return NULL;
}

const char *get_user_config_dir(void)
{
    if (!user_config_dir_cache)
        user_config_dir_cache = get_xdg_dir("XDG_CONFIG_HOME", ".config");
    return user_config_dir_cache;
}

const char *get_user_data_dir(void)
{
    if (!user_data_dir_cache)
        user_data_dir_cache = get_xdg_dir("XDG_DATA_HOME", ".local/share");
    return user_data_dir_cache;
}

const char *get_user_cache_dir(void)
{
    if (!user_cache_dir_cache)
        user_cache_dir_cache = get_xdg_dir("XDG_CACHE_HOME", ".cache");
    return user_cache_dir_cache;
}

const char *get_user_runtime_dir(void)
{
    if (!user_runtime_dir_cache)
        user_runtime_dir_cache = get_xdg_dir("XDG_RUNTIME_DIR", NULL);
    if (!user_runtime_dir_cache || (user_runtime_dir_cache && !user_runtime_dir_cache[0]))
        errx(1, "XDG_RUNTIME_DIR is not set");
    return user_runtime_dir_cache;
}

#ifdef VALGRIND
_destructor_ static void free_dirs(void)
{
    free(home_dir_cache);
    free(user_config_dir_cache);
    free(user_data_dir_cache);
    free(user_cache_dir_cache);
}
#endif
