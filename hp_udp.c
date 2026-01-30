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
 * This module opens UDP server sockets, hiding the IP structure details.
 *
 * To handle broadcast in a multi-homed environment, this module opens
 * two set of sockets:
 * - one set to transmit broadcast messages. This set contains one IPv4
 *   socket per network interface (except the local loopback, ignored).
 * - one set of two sockets for reception and unicast transmission,
 *   one IPv4 and the other IPv6. (TBD: using an IPv6 socket for both?)
 *
 * Only the second set is disclosed to the client.
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
#include <ifaddrs.h>

#include "houseportal.h"
#include "houselog.h"

static const char *UdpService = 0;
static int UdpPort = 0;

struct UdpSocketInterface {
   int socket;
   char name[32];
   struct sockaddr_in address;
};

#define UDPBROADCAST_MAX 16
static struct UdpSocketInterface UdpBroadcast[UDPBROADCAST_MAX];
static int UdpBroadcastCount = 0;

static int UdpSockets[2] = {-1, -1}; // 0: IPv4 (mandatory), 1: IPv6 (optional)


static int hp_udp_socket (const char *interface, int family,
                          const struct sockaddr *addr, socklen_t addrlen) {

    int v;
    int s = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
       houselog_trace (HOUSE_FAILURE, "HousePortal",
                       "UDP socket error on %s (%s): %s\n",
                       interface,
                       (family == AF_INET6)?"ipv6":"ipv4", strerror(errno));
       return -1;
    }

    if (family == AF_INET6) {
        v = 1;
        if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v, sizeof(v)) < 0) {
            houselog_trace (HOUSE_FAILURE, "HousePortal",
                            "Cannot set IPV6_V6ONLY on %s: %s",
                            interface, strerror(errno));
        }
    }

    if (bind(s, addr, addrlen) < 0) {
        houselog_trace (HOUSE_FAILURE, "HousePortal",
                        "Cannot bind to %s (%s): %s",
                        interface,
                        (family == AF_INET6)?"ipv6":"ipv4", strerror(errno));
       close(s);
       return -1;
    }

    v = 256 * 1024;
    if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)) < 0) {
        houselog_trace (HOUSE_FAILURE, "HousePortal",
                        "Cannot set receive buffer to %d on %s: %s",
                        v, interface, strerror(errno));
    }
    v = 256 * 1024;
    if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)) < 0) {
        houselog_trace (HOUSE_FAILURE, "HousePortal",
                        "Cannot set send buffer to %d on %s: %s",
                        v, interface, strerror(errno));
    }
    return s;
}

// Open the broadcast transmission sockets, one per network interface.
// This is used if communication with remote servers is allowed.
//
static void hp_udp_enumerate (const char *service) {

    int i;
    for (i = 0; i < UdpBroadcastCount; ++i) {
       close (UdpSockets[i]);
    }
    UdpBroadcastCount = 0;


    struct ifaddrs *cards;
    struct sockaddr_in *ia;

    // Open one UDP client socket for each (real) network interface. This
    // will be used for sending periodic broadcast on each specific network.
    //
    if (getifaddrs(&cards)) {
        houselog_trace (HOUSE_FAILURE, "HousePortal",
                        "getifaddrs() failed: %s", strerror(errno));
        return;
    }

    struct ifaddrs *cursor;
    for (cursor = cards; cursor != 0; cursor = cursor->ifa_next) {

        if ((cursor->ifa_addr == 0) || (cursor->ifa_netmask == 0)) continue;
        if (cursor->ifa_addr->sa_family != AF_INET) continue;

        ia = (struct sockaddr_in *) (cursor->ifa_addr);
        if (ia->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) continue;

        DEBUG printf ("Opening broadcast socket for interface %s (%08x)\n",
                      cursor->ifa_name, ia->sin_addr.s_addr);

        int s = hp_udp_socket (cursor->ifa_name, AF_INET,
                               cursor->ifa_addr, sizeof(struct sockaddr_in));
        if (s < 0) continue;

        i = UdpBroadcastCount;
        UdpBroadcast[i].socket = s;
        snprintf (UdpBroadcast[i].name,
                  sizeof(UdpBroadcast[0].name), "%s", cursor->ifa_name);

        int v = 1;
        if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &v, sizeof(v)) < 0) {
            houselog_trace (HOUSE_FAILURE, "HousePortal",
                            "Cannot enable broadcast on %s: %s",
                            cursor->ifa_name, strerror(errno));
        }
        if (cursor->ifa_ifu.ifu_broadaddr) {
            UdpBroadcast[i].address =
                *((struct sockaddr_in *) (cursor->ifa_ifu.ifu_broadaddr));
        } else {
            ia = (struct sockaddr_in *) (cursor->ifa_netmask);
            int mask = ia->sin_addr.s_addr;
            UdpBroadcast[i].address =
                *((struct sockaddr_in *) (cursor->ifa_addr));
            UdpBroadcast[i].address.sin_addr.s_addr |= (~mask);
        }
        UdpBroadcast[i].address.sin_port = UdpPort;

        houselog_trace (HOUSE_INFO, "HousePortal",
                        "UDP broadcast is open on %s", cursor->ifa_name);

        if (++UdpBroadcastCount >= UDPBROADCAST_MAX) break;
    }
    freeifaddrs(cards);
}

// This is used to transmit unicast, and to listen for, UDP messages.
//
static int hp_udp_listen (const char *service, int local) {

    if (UdpSockets[0] >= 0) close (UdpSockets[0]);
    if (UdpSockets[1] >= 0) close (UdpSockets[1]);
    UdpSockets[0] = UdpSockets[1] = -1;

    struct addrinfo hints = {0};
    struct addrinfo *resolved;
    struct addrinfo *cursor;

    hints.ai_flags = (local?0:AI_PASSIVE) | AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo (0, service, &hints, &resolved)) {
        houselog_trace (HOUSE_INFO, "HousePortal",
                        "getaddrinfo() failed for %s: %s",
                        service, strerror(errno));
        return 0;
    }

    // Most service need IPv4, so don't do anything until we get at least
    // one iPv4 interface.
    // (Apparently it does happen that the IPv4 port is not listed shortly
    // after boot, even while the IPv6 port is??)
    //
    for (cursor = resolved; cursor; cursor = cursor->ai_next) {
        if (cursor->ai_family == AF_INET) break;
    }
    if (!cursor) {
        freeaddrinfo(resolved);
        return 0;
    }

    int count = 0;

    for (cursor = resolved; cursor; cursor = cursor->ai_next) {

        int family = cursor->ai_family;
        if (family != AF_INET && family != AF_INET6) continue;

        DEBUG printf ("Opening unicast socket for port %s (%s)\n",
                      service, (family==AF_INET6)?"ipv6":"ipv4");

        UdpPort = ((struct sockaddr_in *)(cursor->ai_addr))->sin_port;

        int s = hp_udp_socket ("unicast", family,
                               cursor->ai_addr, cursor->ai_addrlen);
        if (s < 0) continue;

        UdpSockets[(family == AF_INET)?0:1] = s;
        count += 1;

        houselog_trace (HOUSE_INFO, "HousePortal",
                        "Unicast UDP port %s is open (%s)",
                        service, (family==AF_INET6)?"ipv6":"ipv4");
    }
    freeaddrinfo(resolved);
    return count;
}

int hp_udp_server (const char *service, int local, int *sockets, int size) {

    if (size < 2) return 0;

    if (!UdpService || strcmp (UdpService, service)) {
        UdpService = strdup(service);
    }

    int count = hp_udp_listen (service, local);
    if (count <= 0) {
        static int AlreadyShown = 0;
        if (AlreadyShown) return 0;
        houselog_trace (HOUSE_INFO, "HousePortal",
                        "UDP port %s is not yet available for IPv4", service);
        AlreadyShown = 1;
        return 0;
    }

    if (!local) {
        hp_udp_enumerate (service);
    }

    sockets[0] = UdpSockets[0];
    if (count > 1) sockets[1] = UdpSockets[1];
    return count;
}


int hp_udp_has_broadcast (void) {
    return (UdpBroadcastCount >= 0);
}

int hp_udp_receive (int socket, char *buffer, int size) {

    return recvfrom (socket, buffer, size, 0, 0, 0);
}

void hp_udp_broadcast (const char *data, int length) {

    int i;
    for (i = 0; i < UdpBroadcastCount; ++i) {
        if (UdpBroadcast[i].socket < 0) continue;
        DEBUG printf ("IP broadcast on %s, port %d\n",
                      UdpBroadcast[i].name,
                      ntohs(UdpBroadcast[i].address.sin_port));
        sendto (UdpBroadcast[i].socket, data, length, 0,
                (struct sockaddr *)(&UdpBroadcast[i].address),
                sizeof(UdpBroadcast[0].address));
    }
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

        int s;
        if (cursor->ai_family == AF_INET) s = UdpSockets[0];
        else if (cursor->ai_family == AF_INET6) s = UdpSockets[1];
        else continue;

        if (s >= 0) {
            DEBUG printf ("Send UDP message to %s:%s (%s)\n",
                          destination, UdpService,
                          (cursor->ai_family == AF_INET)?"ipv4":"ipv6");
            sendto (s, data, length, 0, cursor->ai_addr, cursor->ai_addrlen);
            break; // No need to send it twice.
        }
    }
    freeaddrinfo (resolved);
}

