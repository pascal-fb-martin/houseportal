#!/bin/bash
#
# HouseList: List the House packages that are currently installed.
#
exec /usr/bin/dpkg-query -W --showformat '${db:Status-Abbrev} ${Package;-20} ${binary:Synopsis}\n' | grep -i house | grep '^ii'
