.POSIX:

include config.mk

all: rawm rawm.1 rawm.conf.5

rawm: rawm.o rawm-config.o
	${CC} -o $@ rawm.o rawm-config.o ${LDFLAGS}

rawm.o: rawm.c rawm-config.h
rawm-config.o: rawm-config.c rawm-config.h

rawm.1: rawm.1.scdoc
	scdoc < rawm.1.scdoc > rawm.1

rawm.conf.5: rawm.conf.5.scdoc
	scdoc < rawm.conf.5.scdoc > rawm.conf.5

install: all
	mkdir -p     ${DESTDIR}${PREFIX}/bin
	mkdir -p     ${DESTDIR}${MANPREFIX}/man1
	mkdir -p     ${DESTDIR}${MANPREFIX}/man5
	mkdir -p     ${DESTDIR}${SYSCONFDIR}/rawm
	cp -f rawm   ${DESTDIR}${PREFIX}/bin/
	cp -f rawm.1 ${DESTDIR}${MANPREFIX}/man1/
	cp -f rawm.conf.5 ${DESTDIR}${MANPREFIX}/man5/
	cp -f rawm.conf ${DESTDIR}${SYSCONFDIR}/rawm/rawm.conf
	chmod 0755   ${DESTDIR}${PREFIX}/bin/rawm
	chmod 0644   ${DESTDIR}${MANPREFIX}/man1/rawm.1
	chmod 0644   ${DESTDIR}${MANPREFIX}/man5/rawm.conf.5
	chmod 0644   ${DESTDIR}${SYSCONFDIR}/rawm/rawm.conf

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/rawm
	rm -f ${DESTDIR}${MANPREFIX}/man1/rawm.1
	rm -f ${DESTDIR}${MANPREFIX}/man5/rawm.conf.5
	rm -f ${DESTDIR}${SYSCONFDIR}/rawm/rawm.conf

clean:
	rm -f rawm rawm.o rawm-config.o rawm.1 rawm.conf.5 \
	      tests/config-reference-test tests/config-reference-test.o

tests/config-reference-test: tests/config-reference-test.o rawm-config.o
	${CC} -o $@ tests/config-reference-test.o rawm-config.o ${LDFLAGS}

check: rawm tests/config-reference-test
	./tests/config.sh ./rawm
	./tests/config-reference-test
	./tests/gaps.sh ./rawm

release:
	git tag -a v${VERSION} -m v${VERSION}

.PHONY: all check install uninstall clean release
