qemu-monitor: qemu-monitor.o argbuilder.o

clean:
	${RM} qemu-monitor *.o

.PHONY: all clean install uninstall
