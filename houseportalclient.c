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
 * const char *houseportal_server (void);
 *
 *    Return the name of the actual server running HousePortal.
 *    This server is usually the local host, but it might be different
 *    if the -portal-server option was used.
 *
 * void houseportal_signature (const char *cypher, const char *key);
 *
 *    Set a secret key for message signature. This call causes the client
 *    code to sign all messages to houseportal. The key must match the key
 *    in the houseportal's configuration for the provided paths.
 *
 * void houseportal_declare  (int webport, const char **path, int count);
 *
 *    Register a specific list of redirections. This erase any previous
 *    registrations. The registrations will be sent to houseportal from
 *    within houseportal_background().
 *
 * void houseportal_declare_more (int webport, const char **path, int count);
 *
 *    Register a specific list of redirections. This adds to any previous
 *    registrations. The registrations will be sent to houseportal from
 *    within houseportal_background().
 *
 * void houseportal_background (time_t now);
 *
 *    Periodic function that handles the registration exchanges with
 *    the houseportal service.
 *
 * void houseportal_register (int webport, const char **path, int count);
 * void houseportal_register_more (int webport, const char **path, int count);
 * void houseportal_renew (void);
 *
 *    This API is being deprecated. Please use houseportal_declare,
 *    houseportal_declare_more and houseportal_background instead.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "echttp.h"
#include "houseportalclient.h"
#include "houseportalhmac.h"


#define HOUSEPORTALPACKET 1400

// Preformat the redirection messages that we send when renewing.
// Note that a complete redirection message is:
//    REDIRECT time [host:]port [HIDE] [[service:]path ..] [SHA-256 signature]
// Since the time and signature are variables, so we only preformat:
//    [host:]port [HIDE] [[service:]path ..]
//
static char *HousePortalRegistration[256];
static int   HousePortalRegistrationLength[256];
static int   HousePortalRegistrationCount = 0;

static char HousePortalTemporaryBuffer[256]; // Don't make the name obvious.
static char HousePortalSecondaryBuffer[256]; // Don't make the name obvious.
static int  HousePortalTemporaryLength;

static const char *HousePortalHost = 0;
static const char *HousePortalPort = "70";
static const char *HouseServicelHost = 0;

typedef struct {
    short external;
    short internal;
} PortMapping;
static PortMapping HousePortalPortMap[256];
static int         HousePortalPortMapCount = 0;

void houseportal_initialize (int argc, const char **argv) {

    int i;

    const char *mapping;
    char localhost[256];
    gethostname (localhost, sizeof(localhost));

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-portal-udp-port=", argv[i], &HousePortalPort))
            continue;
        if (echttp_option_match("-portal-server=", argv[i], &HousePortalHost))
            continue;
        if (echttp_option_match("-portal-map=", argv[i], &mapping)) {
            if (sscanf (mapping, "%hd:%hd",
                        &(HousePortalPortMap[HousePortalPortMapCount].external),
                        &(HousePortalPortMap[HousePortalPortMapCount].internal)) == 2) {
                HousePortalPortMapCount += 1;
            }
            continue;
        }
    }

    // If the portal runs on a remote host, specify the service host when
    // registering, since the portal cannot use its own host name.
    //
    if (HousePortalHost) {
        HouseServicelHost = strdup(localhost);
    }

    // If the portal runs on the local host, report the actual name of the host
    // to the application so that it can be advertised to a remote web client.
    //
    if (!HousePortalHost) {
        HousePortalHost = strdup(localhost);
    }

    if (hp_udp_client (HousePortalHost, HousePortalPort) <= 0) {
        fprintf (stderr, "Cannot open UDP sockets to %s:%s\n",
                 HousePortalHost, HousePortalPort);
        exit(1);
    }
}

const char *houseportal_server (void) {
    return HousePortalHost;
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

void houseportal_declare (int webport, const char **path, int count) {

    // Remove any previous registration (but keep the allocated memory).
    //
    HousePortalRegistrationCount = 0;

    houseportal_declare_more (webport, path, count);
}

void houseportal_declare_more (int webport, const char **path, int count) {

    static pid_t MyPid = 0;
    if (!MyPid) MyPid = getpid();

    int i;
    int index;
    int length = 0;
    char dest[256];
    int dlen;
    int hlen = strlen("REDIRECT 12345678901234");
    char *cursor;

    // Adjust the port number to be advertised according to the port mapping
    // that was declared (if any). This is useful if the local system is only
    // accessible though a gateway or firewall. For example a container.
    //
    for (i = 0; i < HousePortalPortMapCount; ++i) {
        if (webport == HousePortalPortMap[i].internal) {
            webport = HousePortalPortMap[i].external;
            break;
        }
    }

    if (count <= 0) return;
    if (HousePortalRegistrationCount >= 256) return;

    // Build the registration messages.
    // There could be more than one, if there are a lot of paths specified.
    //
    if (HouseServicelHost) {
        dlen = snprintf(dest, sizeof(dest),
                        "%s:%d PID:%d", HouseServicelHost, webport, MyPid);
    } else {
        dlen = snprintf(dest, sizeof(dest), "%d PID:%d", webport, MyPid);
    }

    index = HousePortalRegistrationCount;
    if (HousePortalRegistration[index] == 0)
        HousePortalRegistration[index] = malloc(HOUSEPORTALPACKET);
    strncpy (HousePortalRegistration[index], dest, HOUSEPORTALPACKET);
    length = hlen + dlen;
    cursor = HousePortalRegistration[index] + dlen;

    for (i = 0; i < count; ++i) {
        int l = strlen(path[i]);
        if (length + 1 + l >= HOUSEPORTALPACKET) {
            // Split this service registration into multiple packets.
            if (index >= 255) break; // Way too many.
            HousePortalRegistrationLength[index] =
                (int) (cursor - HousePortalRegistration[index]);

            index += 1;
            if (HousePortalRegistration[index] == 0)
                HousePortalRegistration[index] = malloc(HOUSEPORTALPACKET);
            strncpy (HousePortalRegistration[index], dest, HOUSEPORTALPACKET);
            length = hlen + dlen;
            cursor = HousePortalRegistration[index] + dlen;
        }
        strncpy (cursor++, " ", HOUSEPORTALPACKET-length++);
        strncpy (cursor, path[i], HOUSEPORTALPACKET-length);
        length += l;
        cursor += l;
    }
    HousePortalRegistrationLength[index] =
        (int) (cursor - HousePortalRegistration[index]);
    HousePortalRegistrationCount = index+1;
}

// DEPRECATED: use houseportal_declare instead.
//
void houseportal_register (int webport, const char **path, int count) {

    houseportal_declare (webport, path, count);
    houseportal_renew();
}

// DEPRECATED: use houseportal_declare_more instead.
//
void houseportal_register_more (int webport, const char **path, int count) {

    houseportal_declare_more (webport, path, count);
    houseportal_renew();
}

// DEPRECATED: use houseportal_background instead.
// Will eventually become a static function.
//
void houseportal_renew (void) {

    int i;
    int blen;
    int total;
    char buffer[HOUSEPORTALPACKET+256]; // Added space for signature.

    blen = snprintf (buffer, sizeof(buffer), "REDIRECT %ld ", (long)time(0));

    for (i = 0; i < HousePortalRegistrationCount; ++i) {
        if (HousePortalRegistrationLength[i] >= HOUSEPORTALPACKET - blen)
            continue; // Should never happen, but protect ayway.
        memcpy (buffer+blen,
                HousePortalRegistration[i], HousePortalRegistrationLength[i]);
        total = blen+HousePortalRegistrationLength[i];
        buffer[total] = 0; // Needed for HMAC.

        if (HousePortalTemporaryLength) {
            const char *signature =
                houseportalhmac (HousePortalSecondaryBuffer,
                                 HousePortalTemporaryBuffer, buffer);
            if (signature) {
                const char cypher[] = " SHA-256 ";
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

void houseportal_background (time_t now) {

    static time_t LastRenewal = 0;

    if (HousePortalRegistrationCount <= 0) return;
    if (now < LastRenewal + 30) return;
    LastRenewal = now;

    houseportal_renew ();
}

