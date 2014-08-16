CFLAGS := -std=c11 \
	-Wall -Wextra -pedantic \
	-Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes \
	-D_GNU_SOURCE \
	${CFLAGS}

LDLIBS = -ljansson

qemu-monitor: qemu-monitor.o qmp.o argbuilder.o config.o xdg.o util.o

clean:
	${RM} qemu-monitor *.o

.PHONY: all clean install uninstall
