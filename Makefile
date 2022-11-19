.POSIX:

include config.mk

all: dwm

.c.o:
	${CC} -c ${CFLAGS} ${CPPFLAGS} $<

dwm: dwm.o
	${CC} -o $@ ${LDFLAGS} dwm.o

install: all
	install -m 0755 -Dt ${DESTDIR}${PREFIX}/bin/     dwm
	install -m 0644 -Dt ${DESTDIR}${MANPREFIX}/man1/ dwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dwm
	rm -f ${DESTDIR}${MANPREFIX}/man1/dwm.1

clean:
	rm -f dwm dwm.o

.PHONY: all install uninstall clean
