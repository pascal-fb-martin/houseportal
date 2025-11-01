# This script has been designed to be run as part of a package's post install.
# 

# The cleanup action below is meant to remove obsolete files left from
# an early version of HouseNote.
if [ -d /var/lib/house/note/cache ] ; then rm -rf /var/lib/house/note/cache ; fi

# Cleanup the (current) HouseNote cache.
if [ -d /var/cache/house/note ] ; then rm -rf /var/cache/house/note/* ; fi


# Variable HOUSEAPP indicates that this is a house application, which runs
# as a systemd service.
# This creates the house user (if not present) and starts (or restarts)
# the service. The house user may be assigned to additional groups.
#
if [ "x$HOUSEAPP" != "x" ] ; then
   grep -q '^house:' /etc/passwd || useradd -r house -s /usr/sbin/nologin -d /var/lib/house
   if [ "x$HOUSEGROUP" != "x" ] ; then
      usermod -G $HOUSEGROUP house
   fi
   systemctl daemon-reload
   if systemctl is-active --quiet $HOUSEAPP ; then
      systemctl restart $HOUSEAPP
   else
      systemctl enable $HOUSEAPP
      systemctl start $HOUSEAPP
   fi
fi

