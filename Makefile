CFLAGS := -std=c11 \
	-Wall -Wextra -pedantic \
	-Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes \
	-D_GNU_SOURCE \
	${CFLAGS}

qemu-monitor: qemu-monitor.o argbuilder.o util.o

clean:
	${RM} qemu-monitor *.o

.PHONY: all clean install uninstall
