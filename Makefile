
OBJS= hp_udp.o hp_redirect.o houseportal.o houseportalhmac.o
LIBOJS= houselog.o \
        houseconfig.o \
        houseportalclient.o \
        houseportaludp.o \
        houseportalhmac.o \
        housediscover.o

EXPORT_INCLUDE=houselog.h houseconfig.h houseportalclient.h housediscover.h

all: libhouseportal.a houseportal housediscover

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
	gcc -g -O -o houseportal $(OBJS) libhouseportal.a -lechttp -lssl -lcrypto -lrt

housediscover: housediscoverclient.c libhouseportal.a
	gcc -g -O -o housediscover housediscoverclient.c libhouseportal.a -lechttp -lssl -lcrypto -lrt

package:
	mkdir -p packages
	tar -cf packages/houseportal-`date +%F`.tgz houseportal init.debian Makefile

dev:
	cp libhouseportal.a /usr/local/lib
	chown root:root /usr/local/lib/libhouseportal.a
	chmod 644 /usr/local/lib/libhouseportal.a
	cp housediscover /usr/local/bin
	chown root:root /usr/local/bin/housediscover
	chmod 755 /usr/local/bin/housediscover
	cp $(EXPORT_INCLUDE) /usr/local/include
	for i in $(EXPORT_INCLUDE) ; do chown root:root /usr/local/include/$$i ; done
	for i in $(EXPORT_INCLUDE) ; do chmod 644 /usr/local/include/$$i ; done
	mkdir -p /usr/local/share/house/public
	cp public/house.css public/events.js /usr/local/share/house/public
	chown root:root /usr/local/share/house/public/*
	chmod 644 /usr/local/share/house/public/*

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
	mkdir -p /usr/local/share/house/public
	cp public/* /usr/local/share/house/public
	chown root:root /usr/local/share/house/public/*
	chmod 644 /usr/local/share/house/public/*
	touch /etc/default/houseportal
	touch /etc/house/portal.config
	systemctl daemon-reload
	systemctl enable houseportal
	systemctl start houseportal

uninstall:
	for i in $(EXPORT_INCLUDE) ; do rm -f /usr/local/include/$$i ; done
	rm -f /usr/local/bin/housediscover /usr/local/lib/libhouseportal.a
	rm -f /usr/local/bin/houseportal
	systemctl stop houseportal
	systemctl disable houseportal
	rm -f /etc/init.d/houseportal
	systemctl daemon-reload

purge: uninstall
	rm -f /etc/default/houseportal /etc/house/portal.config

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build/usr/local/bin
	cp houseportal build/usr/local/bin
	chmod 755 build/usr/local/bin/houseportal
	mkdir -p build/usr/local/share/house/public
	cp public/* build/usr/local/share/house/public
	chmod 644 build/usr/local/share/house/public/*
	mkdir -p build/etc/default
	touch build/etc/default/houseportal
	mkdir -p build/etc/house
	touch build/etc/house/portal.config
	cd build ; docker build -t houseportal .
	rm -rf build

