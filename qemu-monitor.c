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

#define MONITOR_SOCK "/run/user/1000/monitor"
#define SHUTDOWN_CMD "system_powerdown"

static int create_signalfd(int signum, ...)
{
    va_list ap;
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, signum);

    va_start(ap, signum);
    while ((signum = va_arg(ap, int)))
        sigaddset(&mask, signum);
    va_end(ap);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        return -1;
    return signalfd(-1, &mask, SFD_CLOEXEC);
}

static void launch_qemu(void)
{
    const char *args[] = {
        "qemu-system-x86_64",
        "-enable-kvm",
        "-m", "2G",
        "-vga", "std",
        "-cpu", "host",
        "-smp", "4", "-nographic",
        "-drive", "file=/home/simon/.local/share/vm/sbc.raw,if=virtio,index=0,media=disk,cache=none",
        "-net", "tap,ifname=tap0,script=no,downscript=no",
        "-net", "nic,model=virtio",
        "-monitor", "unix:" MONITOR_SOCK ",server,nowait",
        "-snapshot",
        NULL
    };

    execvp(args[0], (char *const *)args);
    err(1, "failed to exec %s", args[0]);
}

static void shutdown_qemu(void)
{
    socklen_t sa_len;
    union {
        struct sockaddr sa;
        struct sockaddr_un un;
    } sa;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        err(1, "failed to make socket");

    sa.un = (struct sockaddr_un){ .sun_family = AF_UNIX };
    strcpy(sa.un.sun_path, MONITOR_SOCK);

    if (connect(fd, &sa.sa, sizeof(sa)) < 0)
        err(1, "failed to connect to monitor socket");

    dprintf(fd, "%s\n", SHUTDOWN_CMD);
    close(fd);
}

int main(int argc, char *argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        err(1, "failed to fork");
    } else if (pid == 0) {
        close(0);
        close(1);
        close(2);
        setpgid(0, 0);

        launch_qemu();
    }

    int signalfd = create_signalfd(SIGTERM, SIGINT, SIGQUIT, SIGCHLD);
    struct signalfd_siginfo si;

    for (;;) {
        ssize_t nbytes_r = read(signalfd, &si, sizeof(si));
        if (nbytes_r < 0)
            err(EXIT_FAILURE, "failed to read signal");

        switch (si.ssi_signo) {
        case SIGINT:
        case SIGTERM:
        case SIGQUIT:
            printf("Sending vm ACPI halt signal\n");
            fflush(stdout);
            shutdown_qemu();
            break;
        case SIGCHLD:
            return 0;
        }
    }
}
