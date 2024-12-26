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
 *    This function can be called multiple times: in this case existing
 *    UDP sockets are closed and replaced with the new ones.
 *
 * int hp_udp_receive (int socket, char *buffer, int size)
 *
 *    Receive a UDP packet. Returns the length of the data, or -1.
 *
 * void hp_udp_response (const char *data, int length)
 *
 *    Send a data packet to the source address of the last received message.
 *
 * void hp_udp_broadcast (const char *data, int length) {
 *
 *    Send a broadcast packet. Uses IPv4 only (no broadcast on IPv6).
 *
 * void hp_udp_unicast (const char *destination, const char *data, int length);
 *
 *    Send a unicast packet to the specified destination. This might send
 *    the packet multiple times if the destination name matches multiple
 *    addresses, including IPv4 and IPv6 addresses.
 *
 * int hp_udp_has_broadcast (void);
 *
 *    Return true if there is a broadcast socket available, or false
 *    otherwise.
 *
 * LIMITATIONS:
 *
 * Require an IPv4 addresses for broadcast.
 * Only supports local broadcast (address 255.255.255.255).
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
#include "houselog.h"

static union {
    struct sockaddr_in  ipv4;
    struct sockaddr_in6 ipv6;
} UdpReceived;
static unsigned int UdpReceivedLength;

static int UdpReceivedSocket = -1;

static const char *UdpService = 0;

static int Ipv6UdpSocket = -1;
static int BroadcastUdpSocket = -1;
static struct sockaddr_in BroadcastAddress;


int hp_udp_server (const char *service, int local, int *sockets, int size) {

    static int FirstCall = 1;

    int value;
    int count = 0;
    int s;

    struct addrinfo hints = {0};
    struct addrinfo *resolved;
    struct addrinfo *cursor;

    if (!UdpService || strcmp (UdpService, service)) {
        UdpService = strdup(service);
    }

    if (Ipv6UdpSocket >= 0) {
        close(Ipv6UdpSocket);
        Ipv6UdpSocket = -1;
    }
    if (BroadcastUdpSocket >= 0) {
        close(BroadcastUdpSocket);
        BroadcastUdpSocket = -1;
    }

    hints.ai_flags = (local?0:AI_PASSIVE) | AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo (0, service, &hints, &resolved)) return 0;

    // We need IPv4 broadcast, so don't do anything until we get at least
    // one iPv4 interface.
    // (Apparently it does happen that the IPv4 port is not listed shortly
    // after boot, even while the IPv6 port is??)
    //
    for (cursor = resolved; cursor; cursor = cursor->ai_next) {
        if (cursor->ai_family == AF_INET) break;
    }
    if (!cursor) {
        if (FirstCall)
            houselog_trace (HOUSE_INFO, "HousePortal",
                            "UDP port %s is not yet available for IPv4",
                            service);
        FirstCall = 0;
        freeaddrinfo(resolved);
        return 0;
    }

    for (cursor = resolved; cursor; cursor = cursor->ai_next) {

        if (cursor->ai_family != AF_INET && cursor->ai_family != AF_INET6)
            continue;

        if (cursor->ai_family == AF_INET && BroadcastUdpSocket >= 0)
            continue; // We already have the UDP socket for IPv4.

        if (cursor->ai_family == AF_INET6 && Ipv6UdpSocket >= 0)
            continue; // We already have the UDP socket for IPv6.

        DEBUG printf ("Opening socket for port %s (%s)\n",
                      service, (cursor->ai_family==AF_INET6)?"ipv6":"ipv4");

        s = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (s < 0) {
           houselog_trace (HOUSE_FAILURE, "HousePortal",
                           "cannot open socket for port %s: %s\n",
                           service, strerror(errno));
           continue;
        }

        if (cursor->ai_family == AF_INET6) {
            value = 1;
            if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof(value)) < 0) {
                houselog_trace (HOUSE_FAILURE, "HousePortal",
                                "Cannot set IPV6_V6ONLY: %s", strerror(errno));
            }
        }

        if (bind(s, cursor->ai_addr, cursor->ai_addrlen) < 0) {
            houselog_trace (HOUSE_FAILURE, "HousePortal",
                            "Cannot bind to port %s (%s): %s",
                            service,
                            (cursor->ai_family==AF_INET6)?"ipv6":"ipv4",
                            strerror(errno));
           close(s);
           continue;
        }

        value = 256 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) < 0) {
            houselog_trace (HOUSE_FAILURE, "HousePortal",
                            "Cannot set receive buffer to %d: %s",
                            value, strerror(errno));
        }
        value = 256 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)) < 0) {
            houselog_trace (HOUSE_FAILURE, "HousePortal",
                            "Cannot set send buffer to %d: %s",
                            value, strerror(errno));
        }

        if (!local && cursor->ai_family == AF_INET) {
            value = 1;
            if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) < 0) {
                houselog_trace (HOUSE_FAILURE, "HousePortal",
                                "Cannot enable broadcast: %s", strerror(errno));
            }
            BroadcastUdpSocket = s;
            BroadcastAddress.sin_family = AF_INET;
            BroadcastAddress.sin_addr.s_addr = INADDR_BROADCAST;
            BroadcastAddress.sin_port =
                ((struct sockaddr_in *)(cursor->ai_addr))->sin_port;
        }
        if (!local && cursor->ai_family == AF_INET6) {
            Ipv6UdpSocket = s;
        }

        houselog_trace (HOUSE_INFO, "HousePortal",
                        "UDP socket port %s is open (%s)",
                        service,
                        (cursor->ai_family==AF_INET6)?"ipv6":"ipv4");
        sockets[count++] = s;
        if (count >= size) break;
    }
    freeaddrinfo(resolved);

    return count;
}


int hp_udp_has_broadcast (void) {
    return (BroadcastUdpSocket >= 0);
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

void hp_udp_broadcast (const char *data, int length) {

    if (BroadcastUdpSocket < 0) return;

    DEBUG printf ("IPv4 broadcast port %d\n", ntohs(BroadcastAddress.sin_port));
    sendto (BroadcastUdpSocket, data, length, 0,
            (struct sockaddr *)(&BroadcastAddress), sizeof(BroadcastAddress));
}

void hp_udp_unicast (const char *destination, const char *data, int length) {

    struct addrinfo hints = {0};
    struct addrinfo *resolved;
    struct addrinfo *cursor;

    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo (destination, UdpService, &hints, &resolved)) return;

    for (cursor = resolved; cursor; cursor = cursor->ai_next) {

        if (cursor->ai_family == AF_INET && BroadcastUdpSocket >= 0) {
            DEBUG printf ("IPv4 to %s:%s\n", destination, UdpService);
            sendto (BroadcastUdpSocket, data, length, 0,
                    cursor->ai_addr, cursor->ai_addrlen);
        } else if (cursor->ai_family == AF_INET6 && Ipv6UdpSocket >= 0) {
            DEBUG printf ("IPv6 to %s:%s\n", destination, UdpService);
            sendto (Ipv6UdpSocket, data, length, 0,
                    cursor->ai_addr, cursor->ai_addrlen);
        }
    }
    freeaddrinfo (resolved);
}

