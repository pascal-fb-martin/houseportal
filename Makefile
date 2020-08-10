
OBJS= hp_udp.o hp_redirect.o houseportal.o houseportalhmac.o
LIBOJS= houseportalclient.o houseportaludp.o houseportalhmac.o housetrace.o

all: libhouseportal.a houseportal

main: houseportal.c

redirect: hp_redirect.c

clean:
	rm -f *.o *.a houseportal

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

libhouseportal.a: $(LIBOJS)
	ar r $@ $^
	ranlib $@

houseportal: $(OBJS) libhouseportal.a
	gcc -g -O -o houseportal $(OBJS) libhouseportal.a -lechttp -lcrypto -lrt

package:
	mkdir -p packages
	tar -cf packages/houseportal-`date +%F`.tgz houseportal init.debian Makefile

dev:
	cp libhouseportal.a /usr/local/lib
	cp houseportalclient.h /usr/local/include
	chown root:root /usr/local/lib/libhouseportal.a /usr/local/include/houseportalclient.h
	chmod 644 /usr/local/lib/libhouseportal.a /usr/local/include/houseportalclient.h

install: dev
	if [ -e /etc/init.d/houseportal ] ; then systemctl stop houseportal ; fi
	mkdir -p /etc/house
	if [ -e /etc/houseportal/houseportal.config ] ; then mv /etc/houseportal/houseportal.config /etc/house/portal.config; fi
	mkdir -p /usr/local/bin
	rm -f /usr/local/bin/houseportal /etc/init.d/houseportal
	cp houseportal /usr/local/bin
	cp init.debian /etc/init.d/houseportal
	chown root:root /usr/local/bin/houseportal /etc/init.d/houseportal
	chmod 755 /usr/local/bin/houseportal /etc/init.d/houseportal
	touch /etc/default/houseportal
	mkdir -p /etc/houseportal
	touch /etc/house/portal.config
	systemctl daemon-reload
	systemctl enable houseportal
	systemctl start houseportal

uninstall:
	systemctl stop houseportal
	systemctl disable houseportal
	rm -f /usr/local/bin/houseportal /usr/local/lib/libhouseportal.a
	rm -f /etc/init.d/houseportal /usr/local/include/houseportalclient.h
	systemctl daemon-reload

purge: uninstall
	rm -f /etc/default/houseportal /etc/house/portal.config

