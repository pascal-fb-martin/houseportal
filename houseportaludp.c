/* houseportal - A simple Web portal for home servers.
 *
 * Copyright 2020, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * houseportaludp.c - Manage UDP communications.
 *
 * This module opens a UDP server socket, hiding the IP structure details.
 *
 * SYNOPSYS:
 *
 * int hp_udp_client (const char *destination, const char *service);
 *
 *    Open UDP sockets for the specified destination and returns the count
 *    of sockets that was opened (0 indicates failure).
 *
 * void hp_udp_send (const char *data, int length)
 *
 *    Send a data packet.
 *
 * LIMITATIONS:
 *
 * Only supports IPv4 addresses for the time being.
 * Only supports local broadcast (address 255.255.255.255).
 * Only supports one socket per process.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
//#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "houseportalclient.h"

int UdpClient[16];
int UdpClientCount = 0;

int hp_udp_client (const char *destination, const char *service) {

    int value;
    int s;

    static struct addrinfo hints;
    struct addrinfo *resolved;
    struct addrinfo *cursor;

    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo (0, service, &hints, &resolved)) return 0;

    for (cursor = resolved; cursor; cursor = cursor->ai_next) {

        if (cursor->ai_family != AF_INET && cursor->ai_family != AF_INET6)
            continue;

        s = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (s < 0) {
           fprintf (stderr, "cannot open socket for port %s (%s): %s\n",
                    service,
                    (cursor->ai_family==AF_INET6)?"ipv6":"ipv4",
                    strerror(errno));
           continue;
        }

        if (connect(s, cursor->ai_addr, cursor->ai_addrlen) < 0) {
           fprintf (stderr, "cannot connect to port %s (%s): %s\n",
                    service,
                    (cursor->ai_family==AF_INET6)?"ipv6":"ipv4",
                    strerror(errno));
           close(s);
           continue;
        }

        value = 256 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) < 0) {
           fprintf (stderr, "cannot set receive buffer to %d: %s\n",
                    value, strerror(errno));
        }
        value = 256 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)) < 0) {
           fprintf (stderr, "cannot set send buffer to %d: %s\n",
                    value, strerror(errno));
        }

        UdpClient[UdpClientCount++] = s;
        if (UdpClientCount >= 16) break;
    }
    freeaddrinfo(resolved);

    return UdpClientCount;
}


void hp_udp_send (const char *data, int length) {

    int i;
    for (i = 0; i < UdpClientCount; ++i) {
        send (UdpClient[i], data, length, 0);
    }
}

