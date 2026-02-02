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
 * void hp_redirect_list_json (int services, char *buffer, int size);
 *
 *    This function populates the buffer with a JSON string that represents
 *    the current redirect database. If services is not 0, only entries
 *    mapping to a service are listed.
 *
 * void hp_redirect_peers_json (char *buffer, int size);
 *
 *    This function populates the buffer with a JSON string that represents
 *    the active peers.
 *
 * void hp_redirect_service_json (const char *service, char *buffer, int size);
 *
 *    This function populates the buffer with a JSON string that represents
 *    the active targets for the specified service. If service is null, all
 *    services are reported.
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
static const char *PortalPort = "70";

#define MAX_UDP_POINTS 4
static int PortalUdpPoints[MAX_UDP_POINTS];
static int PortalUdpPointsCount = 0;

typedef struct {
    char *path;
    char *service;
    char *target;
    int length;
    int hide;
    pid_t pid;
    time_t start;
    time_t expiration; // 0: will not expire, 1: expired.
} HttpRedirection;

#define REDIRECT_MAX 128
#define REDIRECT_LIFETIME 180

static int RedirectionCount = 0;
static HttpRedirection Redirections[REDIRECT_MAX];

typedef struct {
    char *name;
    time_t expiration;
} PortalPeers;

static int PeerCount = 0;
static PortalPeers Peers[REDIRECT_MAX];

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
    RestrictUdp2Local = 0;
}

static void PruneRedirect (time_t now) {
    int i;
    int pruned = 0;

    for (i = RedirectionCount-1; i >= 0; --i) {
        time_t expiration = Redirections[i].expiration;
        if (expiration == 0 || now < expiration) continue; // Not expired.

        houselog_event ("ROUTE", Redirections[i].path, "REMOVED",
                        "%s", Redirections[i].target);

        echttp_route_remove (Redirections[i].path);

        free (Redirections[i].path);
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
            printf ("REDIRECT %lld%s %s -> %s\n",
                    (long long)(Redirections[i].expiration),
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
        if (r->expiration) {
            echttp_redirect (url);
        } else {
            echttp_permanent_redirect (url);
        }
    } else {
        echttp_error (500, "Unresolvable redirection.");
    }
    return "";
}

static const char *RedirectRouteAsync (const char *method, const char *uri,
                                       const char *data, int length) {

    // For now, this does the same. Might be different in the future.
    return RedirectRoute (method, uri, data, length);
}

static void AddSingleRedirect (int live, int hide, pid_t pid,
                               const char *target,
                               const char *service, const char *path) {

    int i;
    char buffer[1024];
    time_t now = time(0);
    time_t expiration = (live)?now+REDIRECT_LIFETIME:0;

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
            int restarted = 0;
            if (strcmp (Redirections[i].target, target)) {
                free (Redirections[i].target);
                Redirections[i].target = strdup(target);
                restarted = 1;
            }
            if (service) {
                if (!Redirections[i].service) {
                    houselog_event ("ROUTE", path, "UPDATED",
                                    "NOW SERVICE %s", service);
                    Redirections[i].service = strdup(service);
                } else if (strcmp (Redirections[i].service, service)) {
                    houselog_event ("ROUTE", path, "UPDATED",
                                    "SERVICE CHANGED FROM %s TO %s",
                                    Redirections[i].service, service);
                    free (Redirections[i].service);
                    Redirections[i].service = strdup(service);
                }
            } else if (Redirections[i].service) {
                houselog_event ("ROUTE", path, "UPDATED", "NOT A SERVICE");
                free (Redirections[i].service);
                Redirections[i].service = 0;
            }
            if (pid && (pid != Redirections[i].pid)) {
                Redirections[i].pid = pid;
                restarted = 1;
            }
            if (restarted) {
                Redirections[i].start = now;
                houselog_event ("ROUTE", path, "RESTARTED",
                                "SERVICE %s AS %s", service, target);
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
        houselog_event ("ROUTE", p, "ADD",
                        "SERVICE %s AS %s (%s)",
                        service, target, live?"live":"permanent");

        // Since HousePortal will never process any content data on these
        // redirected requests, better not wait and accumulate the data.
        // Respond with a redirect as soon as the HTTP headers have been
        // received, and then HousePortal will ignore the content data
        // until the connection is closed.
        //
        echttp_asynchronous_route
            (echttp_route_match (p, RedirectRoute), RedirectRouteAsync);

        Redirections[RedirectionCount].path = p;
        Redirections[RedirectionCount].target = strdup(target);
        if (service)
            Redirections[RedirectionCount].service = strdup(service);
        else
            Redirections[RedirectionCount].service = 0;
        Redirections[RedirectionCount].length = strlen(path);
        Redirections[RedirectionCount].hide = hide;
        Redirections[RedirectionCount].pid = pid;
        Redirections[RedirectionCount].start = now; // use /proc/[pid]/stat?
        Redirections[RedirectionCount].expiration = expiration;
        RedirectionCount += 1;
    }
}

static void AddRedirect (int live, char **token, int count) {

    int i;
    int hide = 0;
    pid_t pid = 0;
    const char *target = token[0];

    // Decode optional arguments
    //
    for (i = 1; i < count; ++i) {
        if (strcmp ("HIDE", token[i]) == 0) {
            hide = 1;
        } else if (strncmp ("PID:", token[i], 4) == 0) {
            // Static service redirection cannot provide a PID: just
            // ignore any PID argument present if not live.
            if (live) pid = (pid_t)atoi(token[i]+4);
        } else {
            break;
        }
    }

    // Decode the list of services provided.
    //
    for (; i < count; ++i) {
        char *service = 0;
        char *path = token[i];
        char *s = strchr (path, ':');
        if (s) {
            *s = 0;
            service = path;
            path = s+1;
        }
        AddSingleRedirect (live, hide, pid, target, service, path);
    }
}

static void AddOnePeer (const char *name, time_t expiration) {

    int i;

    for (i = 0; i < PeerCount; ++i) {
        if (!strcmp(Peers[i].name, name)) {
            time_t existing = Peers[i].expiration;
            if ((existing > 0) && (existing < expiration)) { // No downgrade.
                if (existing < time(0)) {
                    houselog_event ("PEER", name, "RECOVER",
                                    "%s EXPIRATION WAS DETECTED",
                                    (existing == 1)?"AFTER":"BEFORE");
                }
                Peers[i].expiration = expiration;
                DEBUG printf ("Peer %s updated to %lld\n", name, (long long)expiration);
            }
            return;
        }
    }

    // This is a new peer: add to the list.
    if (PeerCount < REDIRECT_MAX) {
        houselog_event ("PEER", name, "ADD", expiration?"":"PERMANENT");
        Peers[PeerCount].name = strdup(name);
        Peers[PeerCount].expiration = expiration;
        PeerCount += 1;
    }
}

static void AddPeers (int live, char **token, int count) {

    if (!strcmp(HostName, token[0])) return; // Got our own packet.

    int i;
    time_t default_expiration = (live)?time(0)+REDIRECT_LIFETIME:0;

    for (i = 0; i < count; ++i) {
        time_t expiration = default_expiration;
        if (live) {
            // Extract the sender's expiration time.
            char *s = strchr (token[i], '=');
            if (s) {
                expiration = atoll(s+1);
                *s = 0;
            }
        }
        AddOnePeer (token[i], expiration);
    }
}

// Detect expired peers, for logging purpose. This is a way to
// record some artifacts (on other servers) when a server dies silently.
//
static void DetectExpiredPeers (time_t now) {
    int i;
    for (i = 0; i < PeerCount; ++i) {
        time_t expiration = Peers[i].expiration;
        if ((expiration > 1) && (expiration < now)) {
            houselog_event ("PEER", Peers[i].name, "EXPIRE", "");
            Peers[i].expiration = 1; // Do not log it again.
        }
    }
}

static void DecodeMessage (char *buffer, int live) {

    int i, start, count;
    char *token[REDIRECT_MAX];

    // Split the line
    for (i = start = count = 0; buffer[i] >= ' '; ++i) {
        if (buffer[i] == ' ') {
            if (count >= REDIRECT_MAX) {
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
        AddRedirect (live, token+live+1, count-1); // Remove the keyword.

    } else if (strcmp("PEER", token[0]) == 0) {

        if (live) count -= 1; // Do not count the timestamp.
        if (count < 2) {
            houselog_trace (HOUSE_WARNING, "HousePortal",
                            "Incomplete peer (%d argument)", count);
            if (!live) exit(1);
            return;
        }
        AddPeers (live, token+live+1, count-1); // remove the keyword

    } else if (live) {

        return; // Ignore other messages below.

    } else if (strcmp("LOCAL", token[0]) == 0) {

        houselog_trace (HOUSE_INFO, "HousePortal", "LOCAL mode");
        houselog_event ("SYSTEM", "HousePortal", "SET", "LOCAL MODE");
        RestrictUdp2Local = 1;

    } else if (strcmp("SIGN", token[0]) == 0) {

        if (count == 3 && IntermediateDecodeLength < 128) {
            int index = IntermediateDecodeLength++;
            IntermediateDecode[index].method = strdup(token[1]);
            IntermediateDecode[index].value = strdup(token[2]);
            DEBUG printf ("%s signature key\n", token[1]);
            houselog_event ("SYSTEM", "HousePortal", "SET", "SIGNATURE");
        }

    } else {
        houselog_trace (HOUSE_WARNING, "HousePortal",
                        "Invalid keyword %s", token[0]);
        if (!live) exit(1);
    }
}

static void LoadConfig (const char *name) {

    char buffer[1024];
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

static void hp_redirect_open (void) {

    int count;

    while (PortalUdpPointsCount >= 0) {
        echttp_forget (PortalUdpPoints[--PortalUdpPointsCount]);
    }

    count = hp_udp_server (PortalPort, RestrictUdp2Local,
                           PortalUdpPoints, MAX_UDP_POINTS);
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, "HousePortal",
                        "Cannot open UDP sockets for port %s", PortalPort);
        PortalUdpPointsCount = 0;
        return;
    }

    PortalUdpPointsCount = count;
    while (count > 0) {
        echttp_listen (PortalUdpPoints[--count], 1, hp_redirect_udp, 0);
    }
}

static void hp_redirect_publish (time_t now) {

    if (RestrictUdp2Local) return;

    int i;
    int length;
    char buffer[1400];
    int size = sizeof(buffer);

    if (IntermediateDecodeLength) {
        // Reserve enought room for the upcoming HMAC signature.
        size -= houseportalhmac_size (IntermediateDecode[0].method) + 2;
    }

    length = snprintf (buffer, size, "PEER %lld", (long long)now);

    for (i = 0; i < PeerCount; ++i) {
        time_t expiration = Peers[i].expiration;
        int vested = length;
        if (expiration >= now)
            length +=
                snprintf (buffer+length, size-length,
                          " %s=%lld", Peers[i].name, (long long)expiration);
        else if (!expiration)
            length += snprintf (buffer+length, size-length,
                                " %s", Peers[i].name);
        if (length >= size) {
            // The buffer is too small: revert to the last good item and stop.
            length = vested;
            buffer[vested] = 0;
            break;
        }
    }

    if (IntermediateDecodeLength) {
        const char *signature =
            houseportalhmac (IntermediateDecode[0].method,
                             IntermediateDecode[0].value,
                             buffer);
        if (!signature) return;
        length += snprintf (buffer+length, sizeof(buffer)-length,
                            " %s %s", IntermediateDecode[0].method, signature);
    }

    // There are two ways of publishing:
    // * Use broadcast to talk to the discovered peers.
    // * Use explicit unicast for each statically defined peer.
    // We do this because the static peer feature is meant
    // for peers that cannot be reached through broadcast.
    //
    DEBUG printf ("Publish: %s\n", buffer);
    hp_udp_broadcast (buffer, length);
    for (i = 1; i < PeerCount; ++i) { // Do not send to ourself.
        if (Peers[i].expiration == 0)
            hp_udp_unicast (Peers[i].name, buffer, length);
    }
}

void hp_redirect_background (void) {

    static time_t LastCheck = 0;
    time_t now = time(0);
    struct stat fileinfo;

    if (now > LastCheck + 30) {

        // Keep reinitializing the UDP ports until it succeeds.
        if (PortalUdpPointsCount <= 0) hp_redirect_open ();

        int pruned = 0;
        if (stat (ConfigurationPath, &fileinfo) == 0) {
            if (ConfigurationTime != fileinfo.st_mtim.tv_sec) {
                houselog_trace (HOUSE_INFO, "HousePortal",
                                "Configuration file %s changed",
                                ConfigurationPath);
                DeprecatePermanentConfiguration();
                LoadConfig (ConfigurationPath);
                PruneRedirect (now+3000); // Force immediate expiration.
                pruned = 1;
            }
        } else {
            houselog_trace (HOUSE_FAILURE, "HousePortal",
                            "Cannot stat %s", ConfigurationPath);
        }
        if (!pruned) PruneRedirect (now);
        hp_redirect_publish (now);
        LastCheck = now;
    }

    DetectExpiredPeers (now);
}

static int hp_redirect_preamble (time_t now, char *buffer, int size) {

    return snprintf (buffer, size,
                     "{\"host\":\"%s\",\"timestamp\":%lld,\"portal\":{",
                     HostName, (long long)now);
}

void hp_redirect_list_json (int services, char *buffer, int size) {

    int i;
    int length;
    int reclen;
    char *cursor;
    const char *prefix = "";
    time_t now = time(0);
    char service[256];

    length = hp_redirect_preamble (now, buffer, size);
    cursor = buffer + length;

    length += snprintf (cursor, size-length, "\"redirect\":[");
    cursor = buffer + length;

    for (i = 0; i < RedirectionCount; ++i) {

        time_t expiration = Redirections[i].expiration;

        if (Redirections[i].service)
            snprintf (service, sizeof(service),
                      "\"service\":\"%s\",",Redirections[i].service);
        else if (services)
            continue; // Skip if no service was declared.
        else
            service[0] = 0;

        reclen = snprintf (cursor, size-length,
                  "%s{\"start\":%lld,\"path\":\"%*.*s\",%s\"expire\":%lld,\"target\":\"%s\",\"hide\":%s,\"active\":%s}",
                  prefix,
                  (long long)(Redirections[i].start),
                  Redirections[i].length, Redirections[i].length,
                  Redirections[i].path,
                  service,
                  (long long)expiration,
                  Redirections[i].target,
                  Redirections[i].hide?"true":"false",
                  (expiration == 0 || expiration > now)?"true":"false");
        prefix = ",";
        if (length + reclen >= size) break;
        length += reclen;
        cursor += reclen;
    }
    snprintf (cursor, size-length, "]}}");
    buffer[size-1] = 0;
}

void hp_redirect_peers_json (char *buffer, int size) {

    int i;
    int length;
    char *cursor;
    const char *prefix = "";
    time_t now = time(0);

    length = hp_redirect_preamble (now, buffer, size);
    cursor = buffer + length;

    length += snprintf (cursor, size-length, "\"peers\":[");
    cursor = buffer + length;

    for (i = 0; i < PeerCount; ++i) {

        time_t expiration = Peers[i].expiration;

        if (expiration && expiration <= now) continue;

        length +=
            snprintf (cursor, size-length, "%s\"%s\"", prefix, Peers[i].name);
        prefix = ",";
        cursor = buffer + length;
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

    length += snprintf (cursor, size-length,
                        "\"service\":{\"name\":\"%s\",\"url\":[", name);
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

        length += snprintf (cursor, size-length, "%s\"http://%s%s\"",
                            prefix, hostaddress, Redirections[i].path);
        prefix = ",";
        cursor = buffer + length;
    }
    snprintf (cursor, size-length, "]}}}");
    buffer[size-1] = 0;
}

void hp_redirect_start (int argc, const char **argv) {

    int i;

    char hostname[1000];

    gethostname (hostname, sizeof(hostname));
    HostName = strdup(hostname);

    for (i = 1; i < argc; ++i) {
        echttp_option_match ("-config=", argv[i], &ConfigurationPath);
        echttp_option_match ("-portal-port=", argv[i], &PortalPort);
    }

    // List ourself first.
    //
    int port = echttp_port(4);
    if (port == 80) {
        AddOnePeer (HostName, 0);
    } else {
        char hostaddress[128];
        snprintf (hostaddress, sizeof(hostaddress), "%s:%d", HostName, port);
        AddOnePeer (hostaddress, 0);
    }
    LoadConfig (ConfigurationPath);

    hp_redirect_open ();
}

