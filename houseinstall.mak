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
#
# This file is meant to be included from the main make file of dependent
# applications. The main make file must have defined the following:
#
# - Variable HAPP: the name of the specific house dependent application.
#
# - Variable prefix: the path where to install the application, typically
#   /usr or /usr/local.
#
# - Variable DESTDIR (optional): a path where to build the installation.
#   Typically used for building installation packages.
#
# - Target install-app: install the application-specific run-time files,
#   with the exception of any file specific to the init system.
#
# - Target uninstall-app: remove the files installed by target install-app.
#
# - Target purge-app: remove development files shared with dependent
#   applications, if any.
#
# - Target purge-config: delete the local application configuration.
#
# In addition, the unit file for Systemd must be locally named systemd.service
# and the local service script for runit must be named runit.run.
#
# WARNING:
#
# The rules below perform post-install processing, like refreshing
# systemd or runit. These rules were written so that none of that
# post-install is processed when DESTDIR is set. This is tricky..

HMAN=/var/lib/house/note/manuals
HMANCACHE=/var/cache/house/note

# Some common targets that create directories potentially used by
# multiple House applications.

install-generic-preamble:
	$(INSTALL) -m 0755 -d $(DESTDIR)/etc/house
	$(INSTALL) -m 0755 -d $(DESTDIR)/var/lib/house
	$(INSTALL) -m 0755 -d $(DESTDIR)/etc/default
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/share/house/public

install-preamble: install-generic-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(HMAN)/$(HCAT)
	$(INSTALL) -m 0644 -T README.md $(DESTDIR)$(HMAN)/$(HCAT)/$(HAPP).md
	if [ -d $(DESTDIR)$(HMANCACHE) ] ; then rm -rf $(DESTDIR)$(HMANCACHE)/* ; fi

install-dev-preamble: install-generic-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/lib
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/include

uninstall-preamble:
	rm -f $(DESTDIR)$(HMAN)/$(HCAT)/$(HAPP).md
	if [ -d $(DESTDIR)$(HMANCACHE) ] ; then rm -rf $(DESTDIR)$(HMANCACHE)/* ; fi

# Distribution agnostic install for systemd -------------------------

install-systemd:
	$(INSTALL) -m 0755 -d $(DESTDIR)/lib/systemd/system
	$(INSTALL) -m 0644 -T systemd.service $(DESTDIR)/lib/systemd/system/$(HAPP).service
	if [ "x$(DESTDIR)" = "x" ] ; then grep -q '^house:' /etc/passwd || useradd -r house -s /usr/sbin/nologin -d /var/lib/house ; systemctl daemon-reload ; systemctl enable $(HAPP) ; systemctl start $(HAPP) ; fi

uninstall-systemd:
	if [ "x$(DESTDIR)" = "x" ] ; then if [ -e /etc/init.d/$(HAPP) ] ; then systemctl stop $(HAPP) ; systemctl disable $(HAPP) ; fi ; fi
	rm -f $(DESTDIR)/etc/init.d/$(HAPP)
	if [ "x$(DESTDIR)" = "x" ] ; then if [ -e /lib/systemd/system/$(HAPP).service ] ; then systemctl stop $(HAPP) ; systemctl disable $(HAPP) ; rm -f /lib/systemd/system/$(HAPP).service ; systemctl daemon-reload ; fi ; fi
	rm -f $(DESTDIR)/lib/systemd/system/$(HAPP).service

stop-systemd: uninstall-systemd

# This is a do-nothing target, defined so that make files that don't need it
# do not have to define one. Those make files that need that target may
# define their own: the "::" syntax allows multiple (cumulative) definitions.
#
clean-systemd::
	sleep 0

# Distribution agnostic install for runit ---------------------------

install-runit:
	$(INSTALL) -m 0755 -d $(DESTDIR)/etc/sv/$(HAPP)
	$(INSTALL) -m 0755 -T runit.run $(DESTDIR)/etc/sv/$(HAPP)/run
	rm -f $(DESTDIR)/etc/runit/runsvdir/default/$(HAPP)
	ln -s /etc/sv/$(HAPP) $(DESTDIR)/etc/runit/runsvdir/default/$(HAPP)
	if [ "x$(DESTDIR)" = "x" ] ; then /bin/sleep 5 ; /usr/bin/sv up $(HAPP) ; fi

uninstall-runit:
	if [ x$(DESTDIR)" = "x" ] ; then if [ -e /etc/sv/$(HAPP) ] ; then /usr/bin/sv stop $(HAPP) ; /bin/sleep 5 ; fi ; fi
	rm -rf $(DESTDIR)/etc/sv/$(HAPP)
	rm -rf $(DESTDIR)/etc/runit/runsvdir/default/$(HAPP)

stop-runit:
	if [ x$(DESTDIR)" = "x" ] ; then if [ -e /etc/sv/$(HAPP) ] ; then /usr/bin/sv stop $(HAPP) ; fi ; fi

# Debian GNU/Linux install --------------------------------------

debian-package-generic:
	rm -rf build
	install -m 0755 -d build/$(HAPP)/DEBIAN
	cat debian/control | sed "s/{{arch}}/`dpkg --print-architecture`/" > build/$(HAPP)/DEBIAN/control
	chmod 0644 build/$(HAPP)/DEBIAN/control
	install -m 0644 debian/copyright build/$(HAPP)/DEBIAN
	install -m 0644 debian/changelog build/$(HAPP)/DEBIAN
	if [ -e debian/preinst ] ; then install -m 0755 debian/preinst build/$(HAPP)/DEBIAN ; fi
	if [ -e debian/postinst ] ; then install -m 0755 debian/postinst build/$(HAPP)/DEBIAN ; fi
	if [ -e debian/prerm ] ; then install -m 0755 debian/prerm build/$(HAPP)/DEBIAN ; fi
	if [ -e debian/postrm ] ; then install -m 0755 debian/postrm build/$(HAPP)/DEBIAN ; fi
	make DESTDIR=build/$(HAPP) install-package
	cd build/$(HAPP) ; find etc -type f | sed 's/etc/\/etc/' > DEBIAN/conffiles
	chmod 0644 build/$(HAPP)/DEBIAN/conffiles
	cd build ; fakeroot dpkg-deb -b $(HAPP) .

install-debian: install-preamble stop-systemd clean-systemd install-app install-systemd

uninstall-debian: uninstall-preamble uninstall-systemd uninstall-app

purge-debian: uninstall-debian purge-app purge-config

# Devuan GNU/Linux install (using runit) ------------------------

install-devuan: install-preamble stop-runit install-app install-runit

uninstall-devuan: uninstall-preamble uninstall-runit uninstall-app

purge-devuan: uninstall-devuan purge-app purge-config

# Void Linux install --------------------------------------------

install-void: install-preamble stop-runit install-app install-runit

uninstall-void: uninstall-preamble uninstall-runit uninstall-app

purge-void: uninstall-void purge-app purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

