#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <err.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <linux/un.h>

#include "argbuilder.h"
#include "xdg.h"

#define SHUTDOWN_CMD "system_powerdown"

static void make_sigset(sigset_t *mask, ...)
{
    va_list ap;
    int signum;

    sigemptyset(mask);

    va_start(ap, mask);
    while ((signum = va_arg(ap, int)))
        sigaddset(mask, signum);
    va_end(ap);
}

static void launch_qemu(int argc, char *argv[])
{
    char **args = NULL;
    args_t buf;
    args_init(&buf, 32);

    args_append(&buf, "qemu-system-x86_64", "-enable-kvm", NULL);
    /* args_append(&buf, "qemu-system-x86_64", "-enable-kvm", */
    /*             "-m", "2G", */
    /*             "-cpu", "host", */
    /*             "-smp", "4", */
    /*             "-nographic", */
    /*             "-drive", "file=/home/simon/.local/share/vm/sbc.raw,if=virtio,index=0,media=disk,cache=none", */
    /*             "-net", "tap,ifname=tap0,script=no,downscript=no", */
    /*             "-net", "nic,model=virtio", */
    /*             "-rtc", "base=localtime", */
    /*             "-monitor", NULL); */
    for (int i = 0; i < argc; ++i)
        args_printf(&buf, "%s", argv[i]);

    args_printf(&buf, "-monitor");
    args_printf(&buf, "unix:%s/%s-%d,server,nowait", get_user_runtime_dir(), "monitor", getpid());
    args_build_argv(&buf, &args);

    execvp(args[0], args);
    err(1, "failed to exec %s", args[0]);
}

static void shutdown_qemu(pid_t pid)
{
    union {
        struct sockaddr sa;
        struct sockaddr_un un;
    } sa;

    _cleanup_close_ int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        err(1, "failed to make socket");

    sa.un = (struct sockaddr_un){ .sun_family = AF_UNIX };
    snprintf(sa.un.sun_path, UNIX_PATH_MAX, "%s/%s-%d", get_user_runtime_dir(), "monitor", pid);

    if (connect(fd, &sa.sa, sizeof(sa)) < 0) {
        warn("failed to connect to monitor socket");
        return;
    }

    dprintf(fd, "%s\n", SHUTDOWN_CMD);
}

static _noreturn_ void usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <profile>\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help            display this help\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    sigset_t mask;
    make_sigset(&mask, SIGCHLD, SIGTERM, SIGINT, SIGQUIT, 0);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        err(1, "failed to set sigprocmask");

    /* static const struct option opts[] = { */
    /*     { "help",    no_argument,       0, 'h' }, */
    /*     { 0, 0, 0, 0 } */
    /* }; */

    /* for (;;) { */
    /*     int opt = getopt_long(argc, argv, "h", opts, NULL); */
    /*     if (opt == -1) */
    /*         break; */

    /*     switch (opt) { */
    /*     case 'h': */
    /*         usage(stdout); */
    /*         break; */
    /*     default: */
    /*         usage(stderr); */
    /*     } */
    /* } */

    /* argc -= optind; */
    /* argv += optind; */
    argc -= 1;
    argv += 1;

    pid_t pid = fork();
    if (pid < 0) {
        err(1, "failed to fork");
    } else if (pid == 0) {
        setpgid(0, 0);
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0)
            err(1, "failed to set sigprocmask");

        launch_qemu(argc, argv);
    }

    /* Needed twice to guarantee the child gets its own process group.
     * In case the parent is scheduled before the child */
    setpgid(pid, pid);

    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0)
        err(1, "failed to create signalfd");

    for (;;) {
        struct signalfd_siginfo si;

        ssize_t nbytes_r = read(sfd, &si, sizeof(si));
        if (nbytes_r < 0)
            err(EXIT_FAILURE, "failed to read signal");

        switch (si.ssi_signo) {
        case SIGINT:
        case SIGTERM:
        case SIGQUIT:
            printf("Sending ACPI halt signal to vm...\n");
            fflush(stdout);
            shutdown_qemu(pid);
            break;
        case SIGCHLD:
            return 0;
        }
    }
}
