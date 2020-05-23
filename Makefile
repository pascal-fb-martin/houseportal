
OBJS= hp_udp.o hp_redirect.o houseportal.o
LIBOJS= houseportalclient.o houseportaludp.o

all: houseportal libhouseportal.a

main: houseportal.c

redirect: hp_redirect.c

clean:
	rm -f *.o houseportal

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

libhouseportal.a: $(LIBOJS)
	ar ru $@ $^
	ranlib $@

houseportal: $(OBJS)
	gcc -g -O -o houseportal $(OBJS) -lechttp -lrt

package:
	mkdir -p packages
	tar -cf packages/houseportal-`date +%F`.tgz houseportal init.debian Makefile

install:
	if [ -e /etc/init.d/houseportal ] ; then systemctl stop houseportal ; fi
	mkdir -p /usr/local/bin
	rm -f /usr/local/bin/houseportal /etc/init.d/houseportal
	cp houseportal /usr/local/bin
	cp libhouseportal.a /usr/local/lib
	cp houseportalclient.h /usr/local/include
	cp init.debian /etc/init.d/houseportal
	chown root:root /usr/local/bin/houseportal /etc/init.d/houseportal
	chmod 755 /usr/local/bin/houseportal /etc/init.d/houseportal
	chown root:root /usr/local/lib/libhouseportal.a /usr/local/include/houseportalclient.h
	chmod 644 /usr/local/lib/libhouseportal.a /usr/local/include/houseportalclient.h
	touch /etc/default/houseportal
	mkdir -p /etc/houseportal
	touch /etc/houseportal/houseportal.config
	systemctl daemon-reload
	systemctl enable houseportal
	systemctl start houseportal

uninstall:
	systemctl stop houseportal
	systemctl disable houseportal
	rm -f /usr/local/bin/houseportal /usr/local/lib/libhouseportal.a
	rm -f /etc/init.d/houseportal /usr/local/include/houseportalclient.h
	systemctl daemon-reload

