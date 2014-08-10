#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
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

    args_append(&buf, "qemu-system-x86_64", "-enable-kvm",
                "-m", "2G",
                "-cpu", "host",
                "-smp", "4",
                "-nographic",
                "-drive", "file=/home/simon/.local/share/vm/sbc.raw,if=virtio,index=0,media=disk,cache=none",
                "-net", "tap,ifname=tap0,script=no,downscript=no",
                "-net", "nic,model=virtio",
                "-rtc", "base=localtime",
                "-monitor", NULL);
    args_printf(&buf, "unix:%s,server,nowait", MONITOR_SOCK);


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

    if (connect(fd, &sa.sa, sizeof(sa)) < 0) {
        warn("failed to connect to monitor socket");
        return;
    }

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
