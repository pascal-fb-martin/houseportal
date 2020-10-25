/* houseportal - A simple web portal for home servers
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
 * hp_redirect.c - The houseportal's HTTP request redirector.
 *
 * SYNOPSYS:
 *
 * void hp_redirect_start (int argc, const char **argv);
 *
 *    Initialize the HTTP request redirector.
 *
 * void hp_redirect_background (void);
 *
 *    This function should be called periodically. It checks for
 *    and applies configuration changes, prune obsolete items, etc.
 *
 * void hp_redirect_list_json (char *buffer, int size);
 *
 *    This function returns a string that represents the current redirect
 *    database, dumped in the JSON format.
 *
 * void hp_redirect_service_json (const char *service, char *buffer, int size);
 *
 *    This function returns a string that represents the current targets
 *    for the specified service.
 */

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <arpa/inet.h>

#include <time.h>
#include <stdio.h>

#include "houseportal.h"
#include "houselog.h"
#include "houseportalhmac.h"


static const char *ConfigurationPath = "/etc/house/portal.config";
static time_t ConfigurationTime = 0;
static int RestrictUdp2Local = 0;

typedef struct {
    char *path;
    char *service;
    char *target;
    int length;
    int hide;
    time_t expiration;
} HttpRedirection;

#define REDIRECT_MAX 1024
#define REDIRECT_LIFETIME 600

static int RedirectionCount = 0;
static HttpRedirection Redirections[REDIRECT_MAX];

// Cryptographic keys.
//
typedef struct {
    char *method;
    char *value;
} HttpRequest;
static HttpRequest IntermediateDecode[128]; // Don't make the name obvious.
static int IntermediateDecodeLength = 0;

static const char *HostName = 0;

static const HttpRedirection *SearchBestRedirect (const char *path) {

    int i;
    int quality = 0;
    time_t now = time(0);
    HttpRedirection *found = 0;

    if (path[0] == 0 || path[1] == 0) return 0;

    for (i = 0; i < RedirectionCount; ++i) {
        int length = Redirections[i].length;
        time_t expiration = Redirections[i].expiration;

        if (length <= quality) continue;
        if (expiration < now && expiration > 0) continue;
        if (strncmp (Redirections[i].path, path, length)) continue;
        if (path[length] != 0 && path[length] != '/') continue;

        found = Redirections + i;
        quality = length;
    }
    return found;
}

static void DeprecatePermanentConfiguration (void) {
    int i;

    for (i = 0; i < RedirectionCount; ++i) {
        if (Redirections[i].expiration == 0) Redirections[i].expiration = 1;
    }
    for (i = 0; i < IntermediateDecodeLength; ++i) {
        if (IntermediateDecode[i].method) {
            free(IntermediateDecode[i].method);
            IntermediateDecode[i].method = 0;
        }
        if (IntermediateDecode[i].value) {
            free(IntermediateDecode[i].value);
            IntermediateDecode[i].value = 0;
        }
    }
    IntermediateDecodeLength = 0;
}

static void PruneRedirect (time_t deadline) {
    int i, j;
    int pruned = 0;

    for (i = RedirectionCount-1; i >= 0; --i) {
        time_t expiration = Redirections[i].expiration;
        if (expiration == 0 || expiration > deadline) continue;

        houselog_event (time(0), "route", Redirections[i].path, "expired",
                        "%s", Redirections[i].target);

        // Do not free path: the echhtp route still uses it.
        free (Redirections[i].target);
        if (Redirections[i].service) free (Redirections[i].service);

        if (i < RedirectionCount-1) {
            Redirections[i] = Redirections[RedirectionCount-1];
            RedirectionCount -= 1;
        } else {
            RedirectionCount = i;
        }
        pruned = 1;
    }
    if (!pruned) return;

    DEBUG {
        printf ("After pruning:\n");
        for (i = 0; i < RedirectionCount; ++i) {
            printf ("REDIRECT %ld%s %s -> %s\n",
                    Redirections[i].expiration,
                    Redirections[i].hide?" HIDE":"",
                    Redirections[i].path,
                    Redirections[i].target);
        }
    }
}

static const char *RedirectRoute (const char *method, const char *uri,
                                  const char *data, int length) {
    const HttpRedirection *r = SearchBestRedirect (uri);
    if (r) {
        static char url[2048]; // Accessed once after return.
        char parameters[1024];
        if (r->hide) {
            uri += r->length;
            if (uri[0] == 0) uri = "/";
        }
        echttp_parameter_join (parameters, sizeof(parameters));
        if (parameters[0])
           snprintf (url, sizeof(url), "http://%s%s?%s",
                     r->target, uri, parameters);
        else
           snprintf (url, sizeof(url), "http://%s%s", r->target, uri);
        echttp_redirect (url);
    } else {
        echttp_error (500, "Unresolvable redirection.");
    }
    return "";
}


static void AddSingleRedirect (int live, int hide,
                               const char *target,
                               const char *service, const char *path) {

    int i;
    char buffer[1024];
    time_t expiration = (live)?time(0)+REDIRECT_LIFETIME:0;

    if (!strchr(target, ':')) {
       snprintf (buffer, sizeof(buffer), "%s:%s", HostName, target);
       target = buffer;
    }

    // First search if this is just a renewal.
    // It is OK to renew an obsolete entry. A renewal may change everything
    // but the path: target, permanent/live, hide option, etc..
    //
    for (i = 0; i < RedirectionCount; ++i) {
        if (strcmp (Redirections[i].path, path) == 0) {
            if (live && Redirections[i].expiration == 0) return; // Permanent..
            if (strcmp (Redirections[i].target, target)) {
                free (Redirections[i].target);
                Redirections[i].target = strdup(target);
            }
            if (service) {
                if (!Redirections[i].service) {
                    Redirections[i].service = strdup(service);
                } else if (strcmp (Redirections[i].service, service)) {
                    free (Redirections[i].service);
                    Redirections[i].service = strdup(service);
                }
            } else if (Redirections[i].service) {
                free (Redirections[i].service);
                Redirections[i].service = 0;
            }

            Redirections[i].hide = hide;
            Redirections[i].expiration = expiration;
            return;
        }
    }

    // Not a renewal: use a new slot.
    // Do not reuse an expired slot: echttp does not handle dynamic changes
    // to HTTP routes at this time.
    //
    if (RedirectionCount < REDIRECT_MAX) {
        char *p = strdup(path);
        houselog_trace (HOUSE_INFO, p,
                        "add %s route %s to %s%s",
                        live?"live":"permanent",p,target,hide?" (hide)":"");
        houselog_event (time(0), "route", p, "add",
                        "%s (%s)", target, live?"live":"permanent");
        echttp_route_match (p, RedirectRoute);
        Redirections[RedirectionCount].path = p;
        Redirections[RedirectionCount].target = strdup(target);
        if (service)
            Redirections[RedirectionCount].service = strdup(service);
        else
            Redirections[RedirectionCount].service = 0;
        Redirections[RedirectionCount].length = strlen(path);
        Redirections[RedirectionCount].hide = hide;
        Redirections[RedirectionCount].expiration = expiration;
        RedirectionCount += 1;
    }
}

static void AddRedirect (int live, char **token, int count) {

    int i = 1;
    int hide = 0;
    const char *target = token[0];

    if (strcmp ("HIDE", token[1]) == 0) {
        hide = 1;
        i = 2;
    }
    for (; i < count; ++i) {
        char *service = 0;
        char *path = token[i];
        char *s = strchr (path, ':');
        if (s) {
            *s = 0;
            service = path;
            path = s+1;
        }
        AddSingleRedirect (live, hide, target, service, path);
    }
}

static void DecodeMessage (char *buffer, int live) {

    int i, start, count;
    char *token[16];

    // Split the line
    for (i = start = count = 0; buffer[i] >= ' '; ++i) {
        if (buffer[i] == ' ') {
            if (count >= 16) {
                houselog_trace (HOUSE_WARNING, "HousePortal",
                                "Too many tokens at %s", buffer+i);
                if (!live) exit(1);
                return;
            }
            token[count++] = buffer + start;
            do {
               buffer[i] = 0;
            } while (buffer[++i] == ' ');
            start = i;
        }
    }
    buffer[i] = 0;
    token[count++] = buffer + start;

    if (strcmp("REDIRECT", token[0]) == 0) {

        if (live) count -= 1; // Do not count the timestamp.
        if (count < 3) {
            houselog_trace (HOUSE_WARNING, "HousePortal",
                            "Incomplete redirect (%d arguments)", count);
            if (!live) exit(1);
            return;
        }
        AddRedirect (live, token+live+1, count-1); // Remove the timestamp.

    } else if (live) {

        return; // Ignore other messages below.

    } else if (strcmp("LOCAL", token[0]) == 0) {

        houselog_trace (HOUSE_INFO, "HousePortal", "LOCAL mode");
        RestrictUdp2Local = 1;

    } else if (strcmp("SIGN", token[0]) == 0) {

        if (count == 3 && IntermediateDecodeLength < 128) {
            int index = IntermediateDecodeLength++;
            IntermediateDecode[index].method = strdup(token[1]);
            IntermediateDecode[index].value = strdup(token[2]);
            DEBUG printf ("%s signature key\n", token[1]);
        }

    } else {
        houselog_trace (HOUSE_WARNING, "HousePortal",
                        "Invalid keyword %s", token[0]);
        if (!live) exit(1);
    }
}

static void LoadConfig (const char *name) {

    int i, start, count;
    char buffer[1024];
    char *token[16];
    struct stat fileinfo;

    if (stat (name, &fileinfo) == 0) {
        ConfigurationTime = fileinfo.st_mtim.tv_sec;
    }

    FILE *f = fopen (name, "r");
    if (f == 0) {
        houselog_trace (HOUSE_FAILURE, "HousePortal",
                        "Cannot access configuration file %s", name);
        exit(0);
    }

    while (!feof(f)) {
        buffer[0] = 0;
        fgets (buffer, sizeof(buffer), f);
        if (buffer[0] != '#' && buffer[0] > ' ') {
            DecodeMessage (buffer, 0);
        }
    }
    fclose(f);

    if (IntermediateDecodeLength)
        houselog_trace (HOUSE_INFO,
                        "HousePortal", "Registrations must be signed");
}

static int hp_redirect_inspect2 (const char *data,
                                 const char *method, const char *value) {

    int i;
    const char *signature;

    for (i = 0; i < IntermediateDecodeLength; ++i) {

        if (strcmp(IntermediateDecode[i].method, method)) continue;

        signature = houseportalhmac (IntermediateDecode[i].method,
                                     IntermediateDecode[i].value, data);

        if (strcmp(signature, value) == 0) return 1; // Passed.
        DEBUG printf ("Signature %s did not match client signature %s\n",
                      signature, value);
    }

    houselog_trace (HOUSE_WARNING,
                    "HousePortal", "No signature match for %s", data);

    return 0; // Failed all available keys.
}

static int hp_redirect_inspect (char *data, int length) {

    char *cypher = 0;
    char *crypto = 0;
    const char *sha256mark = " SHA-256 ";

    data[length] = 0;

    cypher = strstr (data, sha256mark);
    if (cypher) {
        crypto = cypher + strlen(sha256mark);
        *(cypher++) = 0; // remove signature from registration data.
    }

    // All future cyphers names checked here..

    if (IntermediateDecodeLength <= 0) return 1; // No key. Accept all.

    if (crypto) {
        crypto[-1] = 0; // Split cypher from signature.
        return hp_redirect_inspect2 (data, cypher, crypto);
    }

    return 0; // Not signed, but signature was required.
}

static void hp_redirect_udp (int fd, int mode) {

    int   length;
    char  buffer[1024];

    length = hp_udp_receive (fd, buffer, sizeof(buffer));
    if (length > 0) {
        buffer[length] = 0;
        DEBUG printf ("Received: %s\n", buffer);
        if (hp_redirect_inspect (buffer, length)) {
            DecodeMessage (buffer, 1);
        }
    }
}

void hp_redirect_background (void) {

    static time_t LastCheck = 0;
    time_t now = time(0);
    struct stat fileinfo;
    int pruned = 0;

    if (now > LastCheck + 30) {
        if (stat (ConfigurationPath, &fileinfo) == 0) {
            if (ConfigurationTime != fileinfo.st_mtim.tv_sec) {
                houselog_trace (HOUSE_INFO, "HousePortal",
                                "Configuration file %s changed",
                                ConfigurationPath);
                DeprecatePermanentConfiguration();
                LoadConfig (ConfigurationPath);
                PruneRedirect (now-3000);
                pruned = 1;
            }
        }
        if (!pruned) PruneRedirect (now-3000);
        LastCheck = now;
    }
}

static int hp_redirect_preamble (time_t now, char *buffer, int size) {

    snprintf (buffer, size,
             "{\"portal\":{\"host\":\"%s\",\"timestamp\":%d,", HostName, now);
    return strlen(buffer);
}

void hp_redirect_list_json (char *buffer, int size) {

    int i;
    int length;
    int reclen;
    char *cursor;
    const char *prefix = "";
    time_t now = time(0);
    char service[256];

    length = hp_redirect_preamble (now, buffer, size);
    cursor = buffer + length;

    snprintf (cursor, size-length, "\"redirect\":[");
    length += strlen(cursor);
    cursor = buffer + length;

    for (i = 0; i < RedirectionCount; ++i) {

        time_t expiration = Redirections[i].expiration;

        if (Redirections[i].service)
            snprintf (service, sizeof(service),
                      "\"service\":\"%s\",",Redirections[i].service);
        else
            service[0] = 0;

        snprintf (cursor, size-length,
                  "%s{\"path\":\"%*.*s\",%s\"expire\":%d,\"target\":\"%s\",\"hide\":%s,\"active\":%s}",
                  prefix,
                  Redirections[i].length, Redirections[i].length,
                  Redirections[i].path,
                  service,
                  expiration,
                  Redirections[i].target,
                  Redirections[i].hide?"true":"false",
                  (expiration == 0 || expiration > now)?"true":"false");
        reclen = strlen(cursor);
        prefix = ",";
        if (length + reclen >= size) break;
        length += reclen;
        cursor += reclen;
    }
    snprintf (cursor, size-length, "]}}");
    buffer[size-1] = 0;
}

void hp_redirect_service_json (const char *name, char *buffer, int size) {

    int i;
    int length;
    int port = echttp_port(4);
    char hostaddress[1024];
    char *cursor;
    const char *prefix = "";
    time_t now = time(0);

    length = hp_redirect_preamble (now, buffer, size);
    cursor = buffer + length;

    snprintf (cursor, size-length,
              "\"service\":{\"name\":\"%s\",\"url\":[", name);
    length += strlen(cursor);
    cursor = buffer + length;

    if (port == 80) {
        strncpy (hostaddress, HostName, sizeof(hostaddress));
        hostaddress[sizeof(hostaddress)-1] = 0;
    } else {
        snprintf (hostaddress, sizeof(hostaddress), "%s:%d", HostName, port);
    }

    for (i = 0; i < RedirectionCount; ++i) {

        time_t expiration = Redirections[i].expiration;

        if (expiration && expiration <= now) continue;

        if (!Redirections[i].service) continue;
        if (strcmp(Redirections[i].service, name)) continue;

        snprintf (cursor, size-length, "%s\"http://%s%s\"",
                  prefix, hostaddress, Redirections[i].path);
        length += strlen(cursor);
        prefix = ",";
        cursor = buffer + length;
    }
    snprintf (cursor, size-length, "]}}}");
    buffer[size-1] = 0;
}

void hp_redirect_start (int argc, const char **argv) {

    int i;
    const char *port = "70";
    int udp[16];
    int count;

    char hostname[1000];

    gethostname (hostname, sizeof(hostname));
    HostName = strdup(hostname);

    for (i = 1; i < argc; ++i) {
        echttp_option_match ("-config=", argv[i], &ConfigurationPath);
        echttp_option_match ("-portal-port=", argv[i], &port);
    }

    LoadConfig (ConfigurationPath);

    count = hp_udp_server (port, RestrictUdp2Local, udp, 16);
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, "HousePortal",
                        "Cannot open UDP sockets for port %s", port);
        exit(1);
    }

    while (count > 0) {
        echttp_listen (udp[--count], 1, hp_redirect_udp, 0);
    }
}

