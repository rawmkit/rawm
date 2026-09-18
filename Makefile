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
	      tests/config-reference-test tests/config-reference-test.o \
	      tests/dialog-center-client tests/dialog-center-client.o \
	      tests/fullscreen-rule-client tests/fullscreen-rule-client.o \
	      tests/attention-rule-client tests/attention-rule-client.o \
	      tests/status-client tests/status-client.o

tests/config-reference-test: tests/config-reference-test.o rawm-config.o
	${CC} -o $@ tests/config-reference-test.o rawm-config.o ${LDFLAGS}

tests/dialog-center-client: tests/dialog-center-client.o
	${CC} -o $@ tests/dialog-center-client.o -L${X11LIB} -lX11

tests/fullscreen-rule-client: tests/fullscreen-rule-client.o
	${CC} -o $@ tests/fullscreen-rule-client.o -L${X11LIB} -lX11

tests/attention-rule-client: tests/attention-rule-client.o
	${CC} -o $@ tests/attention-rule-client.o -L${X11LIB} -lX11

tests/status-client: tests/status-client.o
	${CC} -o $@ tests/status-client.o -L${X11LIB} -lX11

check: rawm tests/config-reference-test tests/dialog-center-client tests/fullscreen-rule-client tests/attention-rule-client tests/status-client
	./tests/config.sh ./rawm
	./tests/config-reference-test
	./tests/gaps.sh ./rawm
	./tests/dialog-center.sh ./rawm ./tests/dialog-center-client
	./tests/fullscreen-rule.sh ./rawm ./tests/fullscreen-rule-client
	./tests/attention-rule.sh ./rawm ./tests/attention-rule-client
	./tests/status.sh ./rawm ./tests/status-client
	./tests/signals.sh ./rawm

release:
	git tag -a v${VERSION} -m v${VERSION}

.PHONY: all check install uninstall clean release
