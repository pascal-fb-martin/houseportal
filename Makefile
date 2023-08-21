
OBJS= hp_udp.o hp_redirect.o houseportal.o houseportalhmac.o
LIBOJS= houselog.o \
        houseconfig.o \
        houseportalclient.o \
        houseportaludp.o \
        houseportalhmac.o \
        housedepositor.o \
        housediscover.o

EXPORT_INCLUDE=houselog.h houseconfig.h houseportalclient.h housediscover.h

# Local build ---------------------------------------------------

all: libhouseportal.a houseportal housediscover housedepositor

clean:
	rm -f *.o *.a houseportal housediscover housedepositor

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

libhouseportal.a: $(LIBOJS)
	ar r $@ $^
	ranlib $@

houseportal: $(OBJS) libhouseportal.a
	gcc -Os -o houseportal $(OBJS) libhouseportal.a -lechttp -lssl -lcrypto -lrt

housediscover: housediscoverclient.c libhouseportal.a
	gcc -Os -o housediscover housediscoverclient.c libhouseportal.a -lechttp -lssl -lcrypto -lrt

housedepositor: housedepositorclient.c libhouseportal.a
	gcc -Os -o housedepositor housedepositorclient.c libhouseportal.a -lechttp -lssl -lcrypto -lrt

# Minimal tar file for installation. ----------------------------

package:
	mkdir -p packages
	tar -cf packages/houseportal-`date +%F`.tgz houseportal housediscover housedepositor $(EXPORT_INCLUDE) libhouseportal.a systemd.service public Makefile

# Distribution agnostic file installation -----------------------

dev:
	cp libhouseportal.a /usr/local/lib
	chown root:root /usr/local/lib/libhouseportal.a
	chmod 644 /usr/local/lib/libhouseportal.a
	cp housediscover /usr/local/bin
	chown root:root /usr/local/bin/housediscover
	chmod 755 /usr/local/bin/housediscover
	cp housedepositor /usr/local/bin
	chown root:root /usr/local/bin/housedepositor
	chmod 755 /usr/local/bin/housedepositor
	cp $(EXPORT_INCLUDE) /usr/local/include
	for i in $(EXPORT_INCLUDE) ; do chown root:root /usr/local/include/$$i ; done
	for i in $(EXPORT_INCLUDE) ; do chmod 644 /usr/local/include/$$i ; done
	mkdir -p /usr/local/share/house/public
	cp public/house.css public/events.js /usr/local/share/house/public
	chown root:root /usr/local/share/house/public/*
	chmod 644 /usr/local/share/house/public/*

install-files: dev
	mkdir -p /etc/house
	if [ -e /etc/houseportal/houseportal.config ] ; then mv /etc/houseportal/houseportal.config /etc/house/portal.config; fi
	mkdir -p /usr/local/bin
	rm -f /usr/local/bin/houseportal
	cp houseportal /usr/local/bin
	chown root:root /usr/local/bin/houseportal
	chmod 755 /usr/local/bin/houseportal
	mkdir -p /usr/local/share/house/public
	cp public/* /usr/local/share/house/public
	icotool -c -o /usr/local/share/house/public/favicon.ico favicon.png
	chown root:root /usr/local/share/house/public/*
	chmod 644 /usr/local/share/house/public/*
	touch /etc/default/houseportal
	touch /etc/house/portal.config

uninstall-files:
	for i in $(EXPORT_INCLUDE) ; do rm -f /usr/local/include/$$i ; done
	rm -f /usr/local/bin/houseportal
	rm -f /usr/local/bin/housediscover
	rm -f /usr/local/bin/housedepositor
	rm -f /usr/local/lib/libhouseportal.a
	rm -f /usr/local/share/house/public/*.html

purge-files:
	rm -f /usr/local/share/house/public/house.css /usr/local/share/house/public/events.js
	rm -f /usr/local/share/house/public/favicon.ico
	rmdir --ignore-fail-on-non-empty /usr/local/share/house/public
	rmdir --ignore-fail-on-non-empty /usr/local/share/house

purge-config:
	rm -f /etc/default/houseportal /etc/house/portal.config

# Distribution agnostic systemd support -------------------------

install-systemd:
	cp systemd.service /lib/systemd/system/houseportal.service
	chown root:root /lib/systemd/system/houseportal.service
	systemctl daemon-reload
	systemctl enable houseportal
	systemctl start houseportal

uninstall-systemd:
	if [ -e /etc/init.d/houseportal ] ; then systemctl stop houseportal ; systemctl disable houseportal ; rm -f /etc/init.d/houseportal ; fi
	if [ -e /lib/systemd/system/houseportal.service ] ; then systemctl stop houseportal ; systemctl disable houseportal ; rm -f /lib/systemd/system/houseportal.service ; systemctl daemon-reload ; fi

stop-systemd: uninstall-systemd

# Distribution agnostic runit support ---------------------------

install-runit:
	mkdir -p /etc/sv/houseportal
	cp runit.run /etc/sv/houseportal/run
	chown root:root /etc/sv/houseportal /etc/sv/houseportal/run
	chmod 755 /etc/sv/houseportal/run
	rm -f /etc/runit/runsvdir/default/houseportal
	ln -s /etc/sv/houseportal /etc/runit/runsvdir/default/houseportal
	/bin/sleep 5
	/usr/bin/sv up houseportal

uninstall-runit:
	if [ -e /etc/sv/houseportal ] ; then /usr/bin/sv shutdown houseportal ; rm -rf /etc/sv/houseportal ; rm -f /etc/runit/runsvdir/default/houseportal ; /bin/sleep 5 ; fi

stop-runit:
	/usr/bin/sv shutdown houseportal

# Debian GNU/Linux install --------------------------------------

install-debian: stop-systemd install-files install-systemd

uninstall-debian: uninstall-systemd uninstall-files

purge-debian: uninstall-debian purge-files purge-config

# Devuan GNU/Linux install (using runit) ------------------------

install-devuan: stop-runit install-files install-runit

uninstall-devuan: uninstall-runit uninstall-files

purge-devuan: uninstall-devuan purge-files purge-config

# Void Linux install --------------------------------------------

install-void: stop-runit install-files install-runit

uninstall-void: uninstall-runit uninstall-files

purge-void: uninstall-void purge-files purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

# Docker install ------------------------------------------------

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

