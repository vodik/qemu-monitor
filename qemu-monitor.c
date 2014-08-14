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
#include "config.h"
#include "xdg.h"

#define NEGOTIATE_CMD "{ \"execute\": \"qmp_capabilities\" }\n"
#define SHUTDOWN_CMD "{ \"execute\": \"system_powerdown\" }\n"

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

static void read_config(const char *config, args_t *buf, bool fullscreen, bool snapshot)
{
    FILE *fp = fopen(config, "r");
    if (fp == NULL) {
        _cleanup_free_ char *profile = NULL;

        asprintf(&profile, "%s/vm/%s.conf", get_user_config_dir(), config);
        fp = fopen(profile, "r");
        if (fp == NULL)
            err(1, "couldn't open %s or %s", config, profile);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    args_init(buf, 32);
    args_append(buf, "qemu-system-x86_64", "-enable-kvm", NULL);

    while ((read = getline(&line, &len, fp)) != -1) {
        _cleanup_free_ char *key = NULL, *value = NULL;
        split_key_value(line, &key, &value);

        if (!key || !value)
            continue;

        if (streq(key, "CPU")) {
            args_append(buf, "-cpu", value, NULL);
        } else if (streq(key, "SMP")) {
            args_append(buf, "-smp", value, NULL);
        } else if (streq(key, "Memory")) {
            args_append(buf, "-m", value, NULL);
        } else if (streq(key, "MemoryFile")) {
            args_append(buf, "-mem-path", value, NULL);
        } else if (streq(key, "Disk")) {
            args_printf(buf, "-drive");
            args_printf(buf, "file=%s,if=virtio,index=0,media=disk,cache=none", value);
        } else if (streq(key, "NetInterface")) {
            args_printf(buf, "-net");
            args_printf(buf, "tap,ifname=%s,script=no,downscript=no", value);
        } else if (streq(key, "NetModel")) {
            args_printf(buf, "-net");
            args_printf(buf, "nic,model=%s", value);
        } else if (streq(key, "RealTimeClock")) {
            args_printf(buf, "-rtc");
            args_printf(buf, "base=%s", value);
        } else if (streq(key, "Graphics")) {
            if (streq(key, "none")) {
                args_append(buf, "-nographic", NULL);
            } else {
                args_append(buf, "-vga", value, NULL);
            }
        } else if (streq(key, "SoundHardware")) {
            args_append(buf, "-soundhw", value, NULL);
        }
    }

    if (fullscreen)
        args_append(buf, "-full-screen", NULL);
    if (snapshot)
        args_append(buf, "-snapshot", NULL);

    if (line)
        free(line);

    args_append(buf, "-monitor", "none", "-qmp", NULL);
    args_printf(buf, "unix:%s/monitor-%d,server,nowait", get_user_runtime_dir(), getpid());
}

static void launch_qemu(args_t *buf)
{
    char **argv;

    args_build_argv(buf, &argv);
    execvp(argv[0], argv);
    err(1, "failed to exec %s", argv[0]);
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
    snprintf(sa.un.sun_path, UNIX_PATH_MAX, "%s/monitor-%d", get_user_runtime_dir(), pid);

    if (connect(fd, &sa.sa, sizeof(sa)) < 0) {
        warn("failed to connect to monitor socket");
        return;
    }

    char buf[BUFSIZ];
    read(fd, buf, sizeof(buf));
    write(fd, NEGOTIATE_CMD, strlen(NEGOTIATE_CMD));
    read(fd, buf, sizeof(buf));
    write(fd, SHUTDOWN_CMD, strlen(SHUTDOWN_CMD));
    read(fd, buf, sizeof(buf));
}

static _noreturn_ void usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <profile>\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help            display this help\n"
        " -f, --fullscreen      start the vm in fullscreen mode (if graphical)\n"
        " -s, --snapshot        write to temporary files instead of the disk image file\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    bool fullscreen = false, snapshot = false;
    args_t buf;

    static const struct option opts[] = {
        { "help",       no_argument, 0, 'h' },
        { "fullscreen", no_argument, 0, 'f' },
        { "snapshot",   no_argument, 0, 's' },
        { 0, 0, 0, 0 }
    };

    for (;;) {
        int opt = getopt_long(argc, argv, "hfs", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'f':
            fullscreen = true;
            break;
        case 's':
            snapshot = true;
            break;
        default:
            usage(stderr);
        }
    }

    const char *config = argv[optind];
    if (!config)
        errx(1, "config not set");

    sigset_t mask;
    make_sigset(&mask, SIGCHLD, SIGTERM, SIGINT, SIGQUIT, 0);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        err(1, "failed to set sigprocmask");

    pid_t pid = fork();
    if (pid < 0) {
        err(1, "failed to fork");
    } else if (pid == 0) {
        setsid();
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0)
            err(1, "failed to set sigprocmask");

        read_config(config, &buf, fullscreen, snapshot);
        launch_qemu(&buf);
    }

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
            switch (si.ssi_code) {
            case CLD_EXITED:
                if (si.ssi_status)
                    warnx("application terminated with error code %d", si.ssi_status);
                return si.ssi_status;
            case CLD_KILLED:
            case CLD_DUMPED:
                errx(1, "application terminated abnormally with signal %d (%s)",
                     si.ssi_status, strsignal(si.ssi_status));
            case CLD_TRAPPED:
            case CLD_STOPPED:
            default:
                break;
            }
            return 0;
        }
    }
}
