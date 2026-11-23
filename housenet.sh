#!/usr/bin/bash
tmpfile=/tmp/netcheck$$
netstat -ant | tee $tmpfile
echo "Host: `/usr/bin/hostname` Total: `grep '^tcp' $tmpfile | wc -l` Listen: `grep LISTEN $tmpfile | wc -l` Established: `grep ESTABLISHED $tmpfile | wc -l` Wait: `grep TIME_WAIT $tmpfile | wc -l`"
rm -f $tmpfile

