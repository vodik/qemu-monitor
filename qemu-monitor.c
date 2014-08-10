#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <err.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include "argbuilder.h"

#define MONITOR_SOCK "/run/user/1000/monitor"
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

static void launch_qemu(void)
{
    char **argv;
    args_t buf;
    args_init(&buf, 32);

    args_printf(&buf, "qemu-system-x86_64");
    args_printf(&buf, "-enable-kvm");
    args_printf(&buf, "-m");
    args_printf(&buf, "2G");
    args_printf(&buf, "-vga");
    args_printf(&buf, "std");
    args_printf(&buf, "-cpu");
    args_printf(&buf, "host");
    args_printf(&buf, "-smp");
    args_printf(&buf, "4");
    args_printf(&buf, "-nographic");
    args_printf(&buf, "-drive");
    args_printf(&buf, "file=/home/simon/.local/share/vm/sbc.raw,if=virtio,index=0,media=disk,cache=none");
    args_printf(&buf, "-net");
    args_printf(&buf, "tap,ifname=tap0,script=no,downscript=no");
    args_printf(&buf, "-net");
    args_printf(&buf, "nic,model=virtio");
    args_printf(&buf, "-rtc");
    args_printf(&buf, "base=localtime");
    args_printf(&buf, "-monitor");
    args_printf(&buf, "unix:" MONITOR_SOCK ",server,nowait");

    args_build_argv(&buf, &argv);
    execvp(argv[0], argv);
    err(1, "failed to exec %s", argv[0]);
}

static void shutdown_qemu(void)
{
    union {
        struct sockaddr sa;
        struct sockaddr_un un;
    } sa;

    _cleanup_close_ int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        err(1, "failed to make socket");

    sa.un = (struct sockaddr_un){ .sun_family = AF_UNIX };
    strcpy(sa.un.sun_path, MONITOR_SOCK);

    if (connect(fd, &sa.sa, sizeof(sa)) < 0)
        err(1, "failed to connect to monitor socket");

    dprintf(fd, "%s\n", SHUTDOWN_CMD);
}

int main(void)
{
    sigset_t mask;
    make_sigset(&mask, SIGCHLD, SIGTERM, SIGINT, SIGQUIT, 0);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        err(1, "failed to set sigprocmask");

    pid_t pid = fork();
    if (pid < 0) {
        err(1, "failed to fork");
    } else if (pid == 0) {
        setpgid(0, 0);
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0)
            err(1, "failed to set sigprocmask");

        launch_qemu();
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
            shutdown_qemu();
            break;
        case SIGCHLD:
            return 0;
        }
    }
}
