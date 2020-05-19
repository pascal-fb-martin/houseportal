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
 * hp_socket.c - Manage UDP communications.
 *
 * This module opens a UDP server socket, hiding the IP structure details.
 *
 * SYNOPSYS:
 *
 * int hp_socket_open (int port, int local)
 *
 *    Open the UDP socket and returns the socket ID. if local is not 0,
 *    this socket is bound to the loopback address, preventing receiving
 *    data from remote machines.
 *
 * void hp_socket_send (const char *data, int length, int address, int port)
 *
 *    Send a data packet to the specified address. If address is 0, the
 *    loopback address is used instead.
 *
 * int hp_socket_receive (char *buffer, int size)
 *
 *    Receive a UDP packet. Returns the length of the data, or -1.
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
#include <arpa/inet.h>

#include "houseportal.h"

static int udpsocket = -1;
static struct sockaddr_in netaddress;


int hp_socket_open (int port, int local) {

    int value;
    int flags;
    int s;

    DEBUG printf ("Opening UDP port %d\n", port);

    s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
       fprintf (stderr, "cannot open socket for port %d: %s\n",
                port, strerror(errno));
       exit (1);
    }

    flags = fcntl(s, F_GETFL, 0);
    fcntl (s, F_SETFL, flags | O_NONBLOCK);

    memset(&netaddress, 0, sizeof(netaddress));
    netaddress.sin_family = AF_INET;
    if (local)
        netaddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else
        netaddress.sin_addr.s_addr = 0;
    netaddress.sin_port = htons(port);

    if (bind(s, (struct sockaddr *)&netaddress, sizeof(netaddress)) < 0) {
       fprintf (stderr, "cannot bind to port %d: %s\n", port, strerror(errno));
       exit (1);
    }

    DEBUG printf ("UDP socket open on port %d%s\n", port, local?" (local)":"");

    value = 256 * 1024;
    if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) < 0) {
       fprintf (stderr, "cannot set receive buffer to %d: %s\n",
                value, strerror(errno));
       exit (1);
    }
    value = 256 * 1024;
    if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)) < 0) {
       fprintf (stderr, "cannot set send buffer to %d: %s\n",
                value, strerror(errno));
       exit (1);
    }

    return udpsocket = s;
}


void hp_socket_send (const char *data, int length, int address, int port) {

    if (address)
        netaddress.sin_addr.s_addr = htonl(address);
    else
        netaddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    netaddress.sin_port = htons(port);

    if (sendto (udpsocket, data, length, 0,
                (struct sockaddr *)&netaddress, sizeof(netaddress)) < 0) {
        fprintf (stderr, "cannot send UDP data: %s\n", strerror(errno));
    }
}


int hp_socket_receive (char *buffer, int size) {

    int length;
    struct sockaddr_in *source;
    socklen_t srclength = sizeof(struct sockaddr_in);

    if (udpsocket < 0) return 0;

    length = recvfrom (udpsocket, buffer, size, 0,
		               (struct sockaddr *)source, &srclength);

    return length;
}

