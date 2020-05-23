/* houseportalclient - A simple web portal for home servers
 *
 * Copyright 2019, Pascal Martin
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
 * client.c - An example on how to use the houseportal's client API.
 *
 * SYNOPSYS:
 *
 * client port [path..]
 *
 *    register the specified port for the specified paths.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "houseportalclient.h"


int main (int argc, const char **argv) {

    const char *port = 0;
    const char *path[1024];
    int count = 0;
    int i;

    houseportal_initialize (argc, argv);

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') continue;
        if (port == 0) {
            port = argv[i];
            continue;
        }
        path[count++] = argv[i];
        if (count >= 1024) break;
    }
    if (count) {
        int portnum = atoi(port);
        printf ("Registering %d redirect paths for port %d\n", count, portnum);
        houseportal_register (portnum, path, count);

        for (i = 0; i < 3000; ++i) {
            sleep (5);
            printf ("Renewing the redirect registration\n");
            houseportal_renew ();
        }
    }
    return 0;
}

