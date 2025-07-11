.POSIX:

include config.mk

all: config.h rawm rawm.1

rawm: rawm.o

rawm.1: rawm.1.scdoc
	scdoc < rawm.1.scdoc > rawm.1

config.h:
	cp config.def.h $@

install: all
	mkdir -p     ${DESTDIR}${PREFIX}/bin
	mkdir -p     ${DESTDIR}${MANPREFIX}/man1
	cp -f rawm   ${DESTDIR}${PREFIX}/bin/
	cp -f rawm.1 ${DESTDIR}${MANPREFIX}/man1/
	chmod 0755   ${DESTDIR}${PREFIX}/bin/rawm
	chmod 0644   ${DESTDIR}${MANPREFIX}/man1/rawm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/rawm
	rm -f ${DESTDIR}${MANPREFIX}/man1/rawm.1

clean:
	rm -f rawm rawm.o rawm.1

release:
	git tag -a v${VERSION} -m v${VERSION}

.PHONY: all install uninstall clean release
