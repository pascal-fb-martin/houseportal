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

# Distribution agnostic install for systemd -------------------------

install-systemd:
	cp systemd.service /lib/systemd/system/$(HAPP).service
	chown root:root /lib/systemd/system/$(HAPP).service
	systemctl daemon-reload
	systemctl enable $(HAPP)
	systemctl start $(HAPP)

uninstall-systemd:
	if [ -e /etc/init.d/$(HAPP) ] ; then systemctl stop $(HAPP) ; systemctl disable $(HAPP) ; rm -f /etc/init.d/$(HAPP) ; fi
	if [ -e /lib/systemd/system/$(HAPP).service ] ; then systemctl stop $(HAPP) ; systemctl disable $(HAPP) ; rm -f /lib/systemd/system/$(HAPP).service ; systemctl daemon-reload ; fi

stop-systemd: uninstall-systemd

# Distribution agnostic install for runit ---------------------------

install-runit:
	mkdir -p /etc/sv/$(HAPP)
	cp runit.run /etc/sv/$(HAPP)/run
	chown root:root /etc/sv/$(HAPP) /etc/sv/$(HAPP)/run
	chmod 755 /etc/sv/$(HAPP)/run
	rm -f /etc/runit/runsvdir/default/$(HAPP)
	ln -s /etc/sv/$(HAPP) /etc/runit/runsvdir/default/$(HAPP)
	/bin/sleep 5
	/usr/bin/sv up $(HAPP)

uninstall-runit:
	if [ -e /etc/sv/$(HAPP) ] ; then /usr/bin/sv stop $(HAPP) ; rm -rf /etc/sv/$(HAPP) ; rm -f /etc/runit/runsvdir/default/$(HAPP) ; /bin/sleep 5 ; fi

stop-runit:
	if [ -e /etc/sv/$(HAPP) ] ; then /usr/bin/sv stop $(HAPP) ; fi

# Debian GNU/Linux install --------------------------------------

install-debian: stop-systemd install-app install-systemd

uninstall-debian: uninstall-systemd uninstall-app

purge-debian: uninstall-debian purge-app purge-config

# Devuan GNU/Linux install (using runit) ------------------------

install-devuan: stop-runit install-app install-runit

uninstall-devuan: uninstall-runit uninstall-app

purge-devuan: uninstall-devuan purge-app purge-config

# Void Linux install --------------------------------------------

install-void: stop-runit install-app install-runit

uninstall-void: uninstall-runit uninstall-app

purge-void: uninstall-void purge-app purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

