#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <getopt.h>
#include <errno.h>
#include <err.h>
#include <signal.h>
#include <poll.h>
#include <sys/signalfd.h>

#include "argbuilder.h"
#include "config.h"
#include "qmp.h"
#include "xdg.h"

struct qemu_config_t {
    char *cpu;
    char *smp;
    char *memory;
    char *memory_file;
    char *disk;
    char *disk_interface;
    char *net_interface;
    char *net_model;
    char *net_macaddr;
    char *rtc;
    char *graphics;
    char *soundhw;
    char *serial;

    bool fullscreen;
    bool snapshot;
};

static sigset_t mask;

static char *random_mac(void)
{
    char *macaddr;
    uint8_t addr[3];

    FILE *urandom = fopen("/dev/urandom", "r");
    fread(addr, sizeof(uint8_t), 3, urandom);
    fclose(urandom);

    asprintf(&macaddr, "52:54:00:%02x:%02x:%02x",
             addr[0], addr[1], addr[2]);
    return macaddr;
}

static void make_sigset(sigset_t *set, ...)
{
    va_list ap;
    int signum;

    sigemptyset(set);

    va_start(ap, set);
    while ((signum = va_arg(ap, int)))
        sigaddset(set, signum);
    va_end(ap);
}

static void read_config(const char *config_file, struct qemu_config_t *config)
{
    _cleanup_fclose_ FILE *fp = NULL;

    if (access(config_file, F_OK) < 0) {
        if (errno != ENOENT)
            err(1, "couldn't open %s", config_file);

        _cleanup_free_ char *profile = NULL;

        asprintf(&profile, "%s/vm/%s.conf", get_user_config_dir(), config_file);
        fp = fopen(profile, "r");
        if (fp == NULL)
            err(1, "couldn't open %s", profile);
    } else {
        fp = fopen(config_file, "r");
        if (fp == NULL)
            err(1, "couldn't open %s", config_file);
    }

    _cleanup_free_ char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, fp)) != -1) {
        _cleanup_free_ char *key = NULL, *value = NULL;
        split_key_value(line, &key, &value);

        if (!key || !value)
            continue;

        if (streq(key, "CPU")) {
            config->cpu = strdup(value);
        } else if (streq(key, "SMP")) {
            config->smp = strdup(value);
        } else if (streq(key, "Memory")) {
            config->memory = strdup(value);
        } else if (streq(key, "MemoryFile")) {
            config->memory_file = strdup(value);
        } else if (streq(key, "Disk")) {
            config->disk = strdup(value);
        } else if (streq(key, "DiskInterface")) {
            config->disk_interface = strdup(value);
        } else if (streq(key, "NetInterface")) {
            config->net_interface = strdup(value);
        } else if (streq(key, "NetModel")) {
            config->net_model = strdup(value);
        } else if (streq(key, "NetMacAddress")) {
            config->net_macaddr = strdup(value);
        } else if (streq(key, "RealTimeClock")) {
            config->rtc = strdup(value);
        } else if (streq(key, "Graphics")) {
            config->graphics = strdup(value);
        } else if (streq(key, "SoundHardware")) {
            config->soundhw = strdup(value);
        } else if (streq(key, "SerialPort")) {
            config->serial = strdup(value);
        }

    }

    /* if mac address isn't set, generate a random one */
    if (!config->net_macaddr)
        config->net_macaddr = random_mac();
}

static void launch_qemu(struct qemu_config_t *config, const char *sockpath)
{
    char **argv;
    args_t buf;

    args_init(&buf, 32);
    args_append(&buf, "qemu-system-x86_64", "-enable-kvm", NULL);

    if (config->cpu)
        args_append(&buf, "-cpu", config->cpu, NULL);
    if (config->smp)
        args_append(&buf, "-smp", config->smp, NULL);
    if (config->memory)
        args_append(&buf, "-m", config->memory, NULL);
    if (config->memory_file)
        args_append(&buf, "-mem-path", config->memory_file, NULL);
    if (config->serial)
        args_append(&buf, "-serial", config->serial, NULL);

    if (config->disk) {
        args_printf(&buf, "-drive");
        if (config->disk_interface)
            args_printf(&buf, "file=%s,if=%s,index=0,media=disk,cache=none", config->disk, config->disk_interface);
        else
            args_printf(&buf, "file=%s,index=0,media=disk,cache=none", config->disk);
    }

    if (config->net_interface) {
        args_printf(&buf, "-net");
        args_printf(&buf, "tap,ifname=%s,script=no,downscript=no", config->net_interface);
    }

    if (config->net_model) {
        args_printf(&buf, "-net");
        args_printf(&buf, "nic,model=%s,macaddr=%s", config->net_model, config->net_macaddr);
    }

    if (config->rtc) {
        args_printf(&buf, "-rtc");
        args_printf(&buf, "base=%s", config->rtc);
    }

    if (config->graphics) {
        if (streq(config->graphics, "none"))
            args_append(&buf, "-nographic", NULL);
        else
            args_append(&buf, "-vga", config->graphics, NULL);
    }

    if (config->soundhw)
        args_append(&buf, "-soundhw", config->soundhw, NULL);
    if (config->fullscreen)
        args_append(&buf, "-full-screen", NULL);
    if (config->snapshot)
        args_append(&buf, "-snapshot", NULL);

    args_append(&buf, "-monitor", "none", "-qmp", NULL);
    args_printf(&buf, "unix:%s", sockpath);

    args_build_argv(&buf, &argv);
    execvp(argv[0], argv);
    err(1, "failed to exec %s", argv[0]);
}

static pid_t fork_qemu(struct qemu_config_t *config, const char *sockpath)
{
    pid_t pid = fork();
    if (pid < 0) {
        err(1, "failed to fork");
    } else if (pid == 0) {
        setsid();
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0)
            err(1, "failed to set sigprocmask");
        launch_qemu(config, sockpath);
    }

    return pid;
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

static int loop(int qmp_fd)
{
    _cleanup_close_ int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    _cleanup_close_ int cfd = qmp_accept(qmp_fd);
    if (sfd < 0)
        err(1, "failed to create signalfd");

    struct pollfd fds[] = {
        { .fd = sfd, .events = POLLIN }
    };
    const size_t fd_count = sizeof(fds) / sizeof(fds[0]);

    while (true) {
        int ret = poll(fds, fd_count, -1);

        if (ret == 0) {
            continue;
        } else if (ret < 0) {
            if (errno == EINTR)
                continue;
            err(EXIT_FAILURE, "failed to poll");
        }

        if (!(fds[0].revents & POLLIN))
            continue;

        struct signalfd_siginfo si;

        ssize_t nbytes_r = read(sfd, &si, sizeof(si));
        if (nbytes_r < 0)
            err(EXIT_FAILURE, "failed to read signal");

        switch (si.ssi_signo) {
        case SIGINT:
        case SIGTERM:
            printf("Sending ACPI halt signal to vm...\n");
            fflush(stdout);
            qmp_command(cfd, "system_powerdown");
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

int main(int argc, char *argv[])
{
    static const struct option opts[] = {
        { "help",       no_argument, 0, 'h' },
        { "fullscreen", no_argument, 0, 'f' },
        { "snapshot",   no_argument, 0, 's' },
        { 0, 0, 0, 0 }
    };

    struct qemu_config_t config = {
        .fullscreen = false,
        .snapshot = false
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
            config.fullscreen = true;
            break;
        case 's':
            config.snapshot = true;
            break;
        default:
            usage(stderr);
        }
    }

    _cleanup_free_ char *sockpath = qmp_sockpath();
    _cleanup_close_ int qmp_fd = qmp_listen(sockpath);

    const char *config_file = argv[optind];
    if (!config_file)
        errx(1, "config not set");
    read_config(config_file, &config);

    make_sigset(&mask, SIGCHLD, SIGTERM, SIGINT, 0);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        err(1, "failed to set sigprocmask");

    fork_qemu(&config, sockpath);
    return loop(qmp_fd);
}
