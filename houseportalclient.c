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
 * houseportalclient.c - The houseportal program's client API.
 *
 * SYNOPSYS:
 *
 * void houseportal_initialize (int argc, const char **argv);
 *
 *    Initialize the environment required to register redirections.
 *
 * void houseportal_register (int webport, const char **path, int count);
 *
 *    Register a specific list of redirections. This erase any previous
 *    registrations.
 *
 * void houseportal_register_more (int webport, const char **path, int count);
 *
 *    Register a specific list of redirections. This adds to any previous
 *    registrations.
 *
 * void houseportal_renew (void);
 *
 *    Renew the previous redirections.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "echttp.h"
#include "houseportalclient.h"


#define HOUSEPORTALPACKET 1400

static char *HousePortalRegistration[256];
static int   HousePortalRegistrationLength[256];
static int   HousePortalRegistrationCount = 0;


void houseportal_initialize (int argc, const char **argv) {

    const char *destination = "localhost";
    const char *service = "70";
    int debug = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-portal-port=", argv[i], &service))
            continue;
        if (echttp_option_match("-portal-server=", argv[i], &destination))
            continue;
    }
    if (hp_udp_client (destination, service) <= 0) {
        fprintf (stderr, "Cannot open UDP sockets to %s:%s\n",
                 destination, service);
        exit(1);
    }
}

void houseportal_register (int webport, const char **path, int count) {

    // Remove any previous registration (but keep the allocated memory).
    //
    HousePortalRegistrationCount = 0;

    houseportal_register_more (webport, path, count);
}


void houseportal_register_more (int webport, const char **path, int count) {

    int i;
    int index;
    int length = 0;
    char buffer[11];
    const char *template = "REDIRECT 12345678901234 "; // Only for size.
    int tlen = strlen(template);
    int blen;
    char *cursor;

    if (count <= 0) return;
    if (HousePortalRegistrationCount >= 256) return;

    // Build the registration messages.
    // There could be more than one, if there are a lot of paths specified.
    //
    snprintf(buffer, sizeof(buffer), "%d", webport);
    blen = strlen(buffer);

    index = HousePortalRegistrationCount;
    if (HousePortalRegistration[index] == 0)
        HousePortalRegistration[index] = malloc(HOUSEPORTALPACKET);
    strncpy (HousePortalRegistration[index], buffer, HOUSEPORTALPACKET);
    length = tlen + blen;
    cursor = HousePortalRegistration[index] + blen;

    for (i = 0; i < count; ++i) {
        int l = strlen(path[i]);
        if (length + 1 + l >= HOUSEPORTALPACKET) {
            if (index >= 255) break; // Way too many.
            HousePortalRegistrationLength[index] =
                (int) (cursor - HousePortalRegistration[index]);

            index += 1;
            if (HousePortalRegistration[index] == 0)
                HousePortalRegistration[index] = malloc(HOUSEPORTALPACKET);
            strncpy (HousePortalRegistration[index], buffer, HOUSEPORTALPACKET);
            length = tlen + blen;
            cursor = HousePortalRegistration[index] + blen;
        }
        strncpy (cursor++, " ", HOUSEPORTALPACKET-length++);
        strncpy (cursor, path[i], HOUSEPORTALPACKET-length);
        length += l;
        cursor += l;
    }
    HousePortalRegistrationLength[index] =
        (int) (cursor - HousePortalRegistration[index]);
    HousePortalRegistrationCount = index+1;

    houseportal_renew();
}

void houseportal_renew (void) {

    int i;
    int blen;
    char buffer[HOUSEPORTALPACKET];

    snprintf (buffer, HOUSEPORTALPACKET, "REDIRECT %ld ", (long)time(0));
    blen = strlen(buffer);

    for (i = 0; i < HousePortalRegistrationCount; ++i) {
        memcpy (buffer+blen,
                HousePortalRegistration[i], HousePortalRegistrationLength[i]);
        hp_udp_send (buffer, blen+HousePortalRegistrationLength[i]);
    }
}

