.POSIX:

include config.mk

all: dwm dwm.1

.c.o:
	${CC} -c ${CFLAGS} ${CPPFLAGS} $<

config.h:
	cp config.def.h $@

dwm.o: config.h

dwm: dwm.o
	${LD} dwm.o ${LDFLAGS} -o $@

dwm.1:
	sed "s/@VERSION@/${VERSION}/g" dwm.1.in > $@

install: all
	mkdir -p    ${DESTDIR}${PREFIX}/bin
	mkdir -p    ${DESTDIR}${MANPREFIX}/man1
	cp -f dwm   ${DESTDIR}${PREFIX}/bin/
	cp -f dwm.1 ${DESTDIR}${MANPREFIX}/man1/
	chmod 0755  ${DESTDIR}${PREFIX}/bin/dwm
	chmod 0644  ${DESTDIR}${MANPREFIX}/man1/dwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dwm
	rm -f ${DESTDIR}${MANPREFIX}/man1/dwm.1

clean:
	rm -f dwm dwm.o dwm.1

.PHONY: all install uninstall clean
