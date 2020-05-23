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
 * void houseportal_signature (const char *cypher, const char *key);
 *
 *    Set a secret key for message signature. This call causes the client
 *    code to sign all messages to houseportal. The key must match the key
 *    in the houseportal's configuration for the provided paths.
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
#include "houseportalhmac.h"


#define HOUSEPORTALPACKET 1400

static char *HousePortalRegistration[256];
static int   HousePortalRegistrationLength[256];
static int   HousePortalRegistrationCount = 0;

static char HousePortalTemporaryBuffer[256]; // Don't make the name obvious.
static char HousePortalSecondaryBuffer[256]; // Don't make the name obvious.
static int  HousePortalTemporaryLength;

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

void houseportal_signature (const char *cypher, const char *key) {
    strncpy (HousePortalSecondaryBuffer,
             cypher, sizeof(HousePortalSecondaryBuffer));
    HousePortalSecondaryBuffer[128] = 0;
    strncpy (HousePortalTemporaryBuffer,
             key, sizeof(HousePortalTemporaryBuffer));
    HousePortalTemporaryBuffer[128] = 0;
    HousePortalTemporaryLength = strlen(key) / 16; // Key must be long enough
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
    int total;
    char buffer[HOUSEPORTALPACKET];
    char signature[HOUSEPORTALPACKET];

    snprintf (buffer, HOUSEPORTALPACKET, "REDIRECT %ld ", (long)time(0));
    blen = strlen(buffer);

    for (i = 0; i < HousePortalRegistrationCount; ++i) {
        memcpy (buffer+blen,
                HousePortalRegistration[i], HousePortalRegistrationLength[i]);
        total = blen+HousePortalRegistrationLength[i];
        buffer[total] = 0; // Needed for HMAC.

        if (HousePortalTemporaryLength) {
            const char *signature =
                houseportalhmac (HousePortalSecondaryBuffer,
                                 HousePortalTemporaryBuffer, buffer);
            if (signature) {
                const char *cypher = " SHA-256 ";
                int cypherlen = sizeof(cypher)-1;
                int signlen = strlen(signature);
                memcpy (buffer+total, " SHA-256 ", cypherlen);
                memcpy (buffer+total+cypherlen, signature, signlen);
                total += (cypherlen + signlen);
            }
        }
        hp_udp_send (buffer, total);
    }
}

