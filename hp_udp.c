/* houseport - A simple Web portal for home servers.
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
 * hp_udp.c - Manage UDP communications (server side).
 *
 * This module opens a UDP server socket, hiding the IP structure details.
 *
 * SYNOPSYS:
 *
 * int hp_udp_server (const char *service, int local, int *sockets, int size)
 *
 *    Open as many UDP sockets as needed and returns the list. If local is
 *    not 0, all sockets are bound to the loopback addresses, preventing
 *    from receiving data from remote machines.
 *
 * int hp_udp_receive (int socket, char *buffer, int size)
 *
 *    Receive a UDP packet. Returns the length of the data, or -1.
 *
 * void hp_udp_response (const char *data, int length)
 *
 *    Send a data packet to the source address of the last received message.
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
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "houseportal.h"
#include "housetrace.h"

static union {
    struct sockaddr_in  ipv4;
    struct sockaddr_in6 ipv6;
} UdpReceived;
int UdpReceivedLength;

int UdpReceivedSocket = -1;


int hp_udp_server (const char *service, int local, int *sockets, int size) {

    int value;
    int flags;
    int count = 0;
    int s;

    static struct addrinfo hints;
    struct addrinfo *resolved;
    struct addrinfo *cursor;

    hints.ai_flags = (local?0:AI_PASSIVE) | AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo (0, service, &hints, &resolved)) return 0;

    for (cursor = resolved; cursor; cursor = cursor->ai_next) {

        if (cursor->ai_family != AF_INET && cursor->ai_family != AF_INET6)
            continue;

        DEBUG printf ("Opening socket for port %s (%s)\n",
                      service, (cursor->ai_family==AF_INET6)?"ipv6":"ipv4");

        s = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (s < 0) {
           housetrace_record (HOUSE_FAILURE, "HousePortal",
                              "cannot open socket for port %s: %s\n",
                              service, strerror(errno));
           continue;
        }

        if (cursor->ai_family == AF_INET6) {
            value = 1;
            if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof(value)) < 0) {
                housetrace_record (HOUSE_FAILURE, "HousePortal",
                                   "Cannot set IPV6_V6ONLY: %s",
                                   strerror(errno));
            }
        }

        if (bind(s, cursor->ai_addr, cursor->ai_addrlen) < 0) {
            housetrace_record (HOUSE_FAILURE, "HousePortal",
                               "Cannot bind to port %s (%s): %s",
                               service,
                               (cursor->ai_family==AF_INET6)?"ipv6":"ipv4",
                               strerror(errno));
           close(s);
           continue;
        }

        value = 256 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) < 0) {
            housetrace_record (HOUSE_FAILURE, "HousePortal",
                               "Cannot set receive buffer to %d: %s",
                               value, strerror(errno));
        }
        value = 256 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)) < 0) {
            housetrace_record (HOUSE_FAILURE, "HousePortal",
                               "Cannot set send buffer to %d: %s",
                               value, strerror(errno));
        }

        housetrace_record (HOUSE_INFO, "HousePortal",
                           "UDP socket port %s is open (%s)",
                           service,
                           (cursor->ai_family==AF_INET6)?"ipv6":"ipv4");
        sockets[count++] = s;
        if (count >= size) break;
    }
    return count;
}


int hp_udp_receive (int socket, char *buffer, int size) {

    UdpReceivedSocket = socket;
    return recvfrom (socket, buffer, size, 0,
		             (struct sockaddr *)(&UdpReceived), &UdpReceivedLength);
}


void hp_udp_response (const char *data, int length) {

    sendto (UdpReceivedSocket, data, length, 0,
            (struct sockaddr *)(&UdpReceived), UdpReceivedLength);
}

