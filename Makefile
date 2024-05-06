# houseportal - A simple web portal environment for home servers
#
# Copyright 2023, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

HAPP=houseportal
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Local build ---------------------------------------------------

OBJS= hp_udp.o hp_redirect.o houseportal.o houseportalhmac.o
LIBOJS= houselog.o \
        houseconfig.o \
        houseportalclient.o \
        houseportaludp.o \
        houseportalhmac.o \
        housedepositor.o \
        housediscover.o

EXPORT_INCLUDE=houselog.h houseconfig.h houseportalclient.h housediscover.h

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

# Application installation (distribution agnostic) --------------

dev:
	mkdir -p $(HROOT)/lib
	cp libhouseportal.a $(HROOT)/lib
	chown root:root $(HROOT)/lib/libhouseportal.a
	chmod 644 $(HROOT)/lib/libhouseportal.a
	mkdir -p $(HROOT)/bin
	cp housediscover $(HROOT)/bin
	chown root:root $(HROOT)/bin/housediscover
	chmod 755 $(HROOT)/bin/housediscover
	cp housedepositor $(HROOT)/bin
	chown root:root $(HROOT)/bin/housedepositor
	chmod 755 $(HROOT)/bin/housedepositor
	mkdir -p $(HROOT)/include
	cp $(EXPORT_INCLUDE) $(HROOT)/include
	for i in $(EXPORT_INCLUDE) ; do chown root:root $(HROOT)/include/$$i ; done
	for i in $(EXPORT_INCLUDE) ; do chmod 644 $(HROOT)/include/$$i ; done
	mkdir -p $(SHARE)/public
	chmod 755 $(SHARE) $(SHARE)/public
	cp public/house.css public/events.js $(SHARE)/public
	chown root:root $(SHARE)/public/*
	chmod 644 $(SHARE)/public/*.*
	cp houseinstall.mak $(SHARE)/install.mak
	chown root:root $(SHARE)/install.mak
	chmod 644 $(SHARE)/install.mak
	chmod 644 $(SHARE)/install.mak

install-app: dev
	mkdir -p /etc/house
	if [ -e /etc/houseportal/houseportal.config ] ; then mv /etc/houseportal/houseportal.config /etc/house/portal.config; fi
	mkdir -p $(HROOT)/bin
	chmod 755 $(HROOT)/bin
	rm -f $(HROOT)/bin/houseportal
	cp houseportal $(HROOT)/bin
	chown root:root $(HROOT)/bin/houseportal
	chmod 755 $(HROOT)/bin/houseportal
	mkdir -p $(SHARE)/public
	chown root:root $(SHARE)/public
	chmod 755 $(SHARE) $(SHARE)/public
	cp public/* $(SHARE)/public
	icotool -c -o $(SHARE)/public/favicon.ico favicon.png
	chown root:root $(SHARE)/public/*
	chmod 644 $(SHARE)/public/*
	touch /etc/default/houseportal
	touch /etc/house/portal.config

uninstall-app:
	rm -f $(HROOT)/bin/houseportal
	rm -f $(HROOT)/bin/housediscover
	rm -f $(HROOT)/bin/housedepositor
	rm -f $(SHARE)/public/*.html

purge-app:
	for i in $(EXPORT_INCLUDE) ; do rm -f $(HROOT)/include/$$i ; done
	rm -f $(SHARE)/public/house.css $(SHARE)/public/events.js
	rm -f $(SHARE)/public/favicon.ico
	rm -f $(SHARE)/install.mak
	rmdir --ignore-fail-on-non-empty $(SHARE)/public
	rmdir --ignore-fail-on-non-empty $(SHARE)

purge-config:
	rm -f /etc/default/houseportal /etc/house/portal.config

# System installation. ------------------------------------------

include ./houseinstall.mak

# Docker install ------------------------------------------------

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build$(HROOT)/bin
	cp houseportal build$(HROOT)/bin
	chmod 755 build$(HROOT)/bin/houseportal
	mkdir -p build$(SHARE)/public
	cp public/* build$(SHARE)/public
	chmod 644 build$(SHARE)/public/*
	mkdir -p build/etc/default
	touch build/etc/default/houseportal
	mkdir -p build/etc/house
	touch build/etc/house/portal.config
	cd build ; docker build -t houseportal .
	rm -rf build

