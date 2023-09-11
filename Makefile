.POSIX:

include config.mk

all: config.h dwm

config.h:
	cp config.def.h $@

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	cp -f dwm ${DESTDIR}${PREFIX}/bin/
	sed "s/^\.Os/.Os ${NAME} ${VERSION}/" dwm.1 \
		> ${DESTDIR}${MANPREFIX}/man1/dwm.1
	chmod 0755 ${DESTDIR}${PREFIX}/bin/dwm
	chmod 0644 ${DESTDIR}${MANPREFIX}/man1/dwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dwm
	rm -f ${DESTDIR}${MANPREFIX}/man1/dwm.1

clean:
	rm -f dwm dwm.o
	rm -f ${DIST}.tar.gz

dist: clean
	git archive --format=tar.gz -o ${DIST}.tar.gz --prefix=${DIST}/ HEAD

.PHONY: all install uninstall clean dist
