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

prefix=/usr/local

HAPP=houseportal
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

# Local build ---------------------------------------------------

OBJS= hp_udp.o \
      hp_redirect.o \
      houseportal.o \
      houseportalhmac.o \
      houselog_nostorage.o

LIBOJS= houselog_live.o \
        houselog_sensor.o \
        houselog_storage.o \
        houseconfig.o \
        houseportalclient.o \
        houseportaludp.o \
        houseportalhmac.o \
        housedepositor.o \
        housedepositorstate.o \
        housealmanac.o \
        housediscover.o

EXPORT_INCLUDE=houselog.h \
               houseconfig.h \
               houseportalclient.h \
               housediscover.h \
               housedepositor.h \
               housedepositorstate.h \
               housealmanac.h \
               houselog_sensor.h \
               houselog_storage.h

all: libhouseportal.a houseportal housediscover housedepositor housegetalmanac

clean:
	rm -f *.o *.a houseportal housediscover housedepositor housegetalmanac

rebuild: clean all

%.o: %.c
	gcc -c -Wall -g -Os -fPIC -o $@ $<

libhouseportal.a: $(LIBOJS)
	ar r $@ $^
	ranlib $@

houseportal: $(OBJS) libhouseportal.a
	gcc -g -Os -o houseportal $(OBJS) libhouseportal.a -lechttp -lssl -lcrypto -lrt

housediscover: housediscoverclient.c libhouseportal.a
	gcc -Os -o housediscover housediscoverclient.c libhouseportal.a -lechttp -lssl -lcrypto -lrt

housedepositor: housedepositorclient.c libhouseportal.a
	gcc -Os -o housedepositor housedepositorclient.c libhouseportal.a -lechttp -lssl -lcrypto -lrt

housegetalmanac: housegetalmanac.c libhouseportal.a
	gcc -Os -o housegetalmanac housegetalmanac.c libhouseportal.a -lechttp -lssl -lcrypto -lrt

# Minimal tar file for installation. ----------------------------

package:
	mkdir -p packages
	tar -cf packages/houseportal-`date +%F`.tgz houseportal housediscover housedepositor $(EXPORT_INCLUDE) libhouseportal.a systemd.service public Makefile

# Application installation (distribution agnostic) --------------

dev:
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/lib
	$(INSTALL) -m 0644 libhouseportal.a $(DESTDIR)$(prefix)/lib
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -s housediscover $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -s housedepositor $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -s housegetalmanac $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/include
	$(INSTALL) -m 0644 $(EXPORT_INCLUDE) $(DESTDIR)$(prefix)/include
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public
	$(INSTALL) -m 0644 public/house.css public/events.js $(DESTDIR)$(SHARE)/public
	$(INSTALL) -m 0644 -T houseinstall.mak $(DESTDIR)$(SHARE)/install.mak

install-ui:
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public
	icotool -c -o $(DESTDIR)$(SHARE)/public/favicon.ico favicon.png
	chown root:root $(DESTDIR)$(SHARE)/public/favicon.ico
	chmod 644 $(DESTDIR)$(SHARE)/public/favicon.ico

install-app: dev install-ui
	$(INSTALL) -m 0755 -d $(DESTDIR)/etc/house
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -s houseportal $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -T roof.sh $(DESTDIR)$(prefix)/bin/roof
	touch $(DESTDIR)/etc/default/housegeneric
	touch $(DESTDIR)/etc/default/houseportal
	touch $(DESTDIR)/etc/house/portal.config

uninstall-app:
	rm -f $(DESTDIR)$(prefix)/bin/houseportal
	rm -f $(DESTDIR)$(prefix)/bin/housediscover
	rm -f $(DESTDIR)$(prefix)/bin/housedepositor
	rm -f $(DESTDIR)$(prefix)/bin/roof
	rm -f $(DESTDIR)$(SHARE)/public/*.html

purge-app:
	for i in $(EXPORT_INCLUDE) ; do rm -f $(DESTDIR)$(prefix)/include/$$i ; done
	rm -f $(DESTDIR)$(SHARE)/public/house.css $(DESTDIR)$(SHARE)/public/events.js
	rm -f $(DESTDIR)$(SHARE)/public/favicon.ico
	rm -f $(DESTDIR)$(SHARE)/install.mak
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(SHARE)/public
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(SHARE)

purge-config:
	rm -f $(DESTDIR)/etc/default/houseportal $(DESTDIR)/etc/house/portal.config

# System installation. ------------------------------------------

include ./houseinstall.mak

# Docker install ------------------------------------------------

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build$(prefix)/bin
	cp houseportal build$(prefix)/bin
	chmod 755 build$(prefix)/bin/houseportal
	mkdir -p build$(SHARE)/public
	cp public/* build$(SHARE)/public
	chmod 644 build$(SHARE)/public/*
	mkdir -p build/etc/default
	touch build/etc/default/housegeneric
	touch build/etc/default/houseportal
	mkdir -p build/etc/house
	touch build/etc/house/portal.config
	cd build ; docker build -t houseportal .
	rm -rf build

