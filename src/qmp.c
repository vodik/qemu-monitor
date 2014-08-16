#include "qmp.h"

#include <err.h>
#include <sys/socket.h>
#include <linux/un.h>
#include <jansson.h>

#include "xdg.h"
#include "util.h"

char *qmp_sockpath(void)
{
    char *socket = NULL;
    asprintf(&socket, "%s/monitor-%d", get_user_runtime_dir(), getpid());
    return socket;
}

int qmp_listen(const char *sockpath)
{
    union {
        struct sockaddr sa;
        struct sockaddr_un un;
    } sa;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        err(1, "failed to make socket");

    sa.un = (struct sockaddr_un){ .sun_family = AF_UNIX };
    strncpy(sa.un.sun_path, sockpath, UNIX_PATH_MAX);

    if (bind(fd, &sa.sa, sizeof(sa)) < 0)
        err(1, "failed to bind monitor socket");

    if (listen(fd, SOMAXCONN) < 0)
        err(1, "failed to listen on monitor socket");

    return fd;
}

int qmp_accept(int fd)
{
    int cfd = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
    if (cfd < 0)
        err(EXIT_FAILURE, "failed to accept connection");

    char buf[BUFSIZ];
    read(cfd, buf, sizeof(buf));
    qmp_command(cfd, "qmp_capabilities");

    return cfd;
}

static int qmp_send(int fd, const char *command)
{
    _cleanup_json_ json_t *root = json_object();
    json_object_set_new(root, "execute", json_string(command));

    _cleanup_free_ char *json = json_dumps(root, 0);
    if (json)
        return dprintf(fd, "%s\r\n", json);
    return 0;
}

static int qmp_recv(int fd)
{
    char buf[BUFSIZ];
    json_error_t error;
    ssize_t nbytes_r = read(fd, buf, sizeof(buf));

    if (nbytes_r < 0) {
        err(1, "failed to read");
    } else if (nbytes_r == 0) {
        return 0;
    }

    _cleanup_json_ json_t *root = json_loadb(buf, nbytes_r, 0, &error);

    json_t *name = json_object_get(root, "return");
    if (!json_is_array(name))
        return -1;
    return 0;
}

int qmp_command(int fd, const char *command)
{
    if (qmp_send(fd, command) < 0)
        return -1;
    if (qmp_recv(fd) < 0)
        return -1;
    return 0;
}
