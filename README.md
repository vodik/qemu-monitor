## qemu-monitor

Quick and dirty wrapper around qemu that starts qemu and nicely shuts
down the vm through ACPI signals on shutdown.

Why c and not bash + socat? This integrates nicely with systemd. I can
take advantage of mixed kill mode to give the vm time to shutdown nicely
*and* make sure its actually killed should the timeout expire:

    [Service]
    ExecStart=/usr/bin/qemu-monitor
    KillMode=mixed
    TimeoutStopSec=3min
