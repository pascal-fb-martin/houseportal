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
 * housediscover.c - The client side of the houseportal discovery.
 *
 * SYNOPSYS:
 *
 * void housediscover_initialize (int argc, const char **argv);
 *
 *    Must be called first, with the command line arguments.
 *
 * void housediscover (void);
 *
 *    Search for all providers for all service on the network.
 *    This is an asynchronous process: the actual discovery will take
 *    place later, when receiving responses. This function must be called
 *    periodically.
 *
 *    Note that the discovery is done in two phase:
 *    - Phase 1: query local portal, to detect all portals, every 10s.
 *    - Phase 2: query all portals to detect services, every 120s or on change.
 *
 *    Doing it this way reduces network traffic (local query does not take
 *    network bandwidth, while still reacting to newly detected portals.
 *
 * int housediscover_changed (const char *service, time_t since);
 *
 *    Return true if something new was discovered since the specified time,
 *    false otherwise.
 *
 * void housediscovered (const char *service, void *context,
 *                       housediscover_consumer *consumer);
 *
 *    Retrieve the result of the latest discovery. This is a classic iterator
 *    design: the consumer function is called for each matching item found.
 *
 * LIMITATIONS:
 *
 * The current implementation never forget a service instance. That is
 * a problem when moving service instances from server to server.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "echttp.h"
#include "echttp_json.h"
#include "echttp_hash.h"

#include "houselog.h"
#include "housediscover.h"

static const char *LocalPortalServer = "localhost";
static int         LocalPortalPort = 80;

static echttp_hash DiscoveryByUrl; // URL is a unique key.
static time_t DiscoveryLatest[ECHTTP_MAX_SYMBOL]; // Each time detected

static echttp_hash DiscoveryByService; // Service name is not unique.
static const char *DiscoveryUrl[ECHTTP_MAX_SYMBOL];
static time_t      DiscoveryFirstDetected[ECHTTP_MAX_SYMBOL];

static time_t DiscoveryRequest = 0;

#define DISCOVERY_PORTAL_INTERVAL 10
#define DISCOVERY_SERVICE_INTERVAL 120

#define DEBUG if (echttp_isdebug()) printf

// Manage the resources used to decode the discovery responses.
static ParserToken *DiscoveryTokens = 0;
static int *DiscoveryInnerList = 0; // That references tokens too: same size.
static int DiscoveryTokensAllocated = 0;

static int housediscover_adjust_tokens (const char *data) {
    if (!data) return 0; // Self protection
    int count = echttp_json_estimate(data);
    if (count >= DiscoveryTokensAllocated) {
        // Need to allocate more than required so that we avoid re-allocating
        // too many times.
        int need = DiscoveryTokensAllocated = count + 128;
        DiscoveryTokens = realloc (DiscoveryTokens, need*sizeof(ParserToken));
        DiscoveryInnerList = realloc (DiscoveryInnerList, need*sizeof(int));
    }
    return count;
}

void housediscover_initialize (int argc, const char **argv) {

    int i;
    const char *port;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-portal-server=", argv[i], &LocalPortalServer))
            continue;
        if (echttp_option_match("-portal-http-port=", argv[i], &port)) {
            LocalPortalPort = atoi(port);
            continue;
        }
    }
    DEBUG ("local portal server: %s:%d\n", LocalPortalServer, LocalPortalPort);
}


static int housediscover_lapsed (time_t timestamp) {
    // Don't substract, only add, to avoid landing into negative range
    // whenever DiscoveryRequest is reset..
    return (timestamp + DISCOVERY_SERVICE_INTERVAL < DiscoveryRequest);
}

static int housediscover_register (const char *name, const char *url) {

    int byurl = echttp_hash_find (&DiscoveryByUrl, url);
    int isnew = 0;
    time_t now = time(0);

    if (byurl <= 0) {
        char *urlsaved = strdup(url);
        byurl = echttp_hash_add (&DiscoveryByUrl, urlsaved);
        if (byurl <= 0) {
            houselog_trace (HOUSE_FAILURE,
                            "cannot register service %s at %s\n", name, url);
            free(urlsaved);
            return 0;
        }
        DEBUG ("registered new service %s at %s\n", name, url);
        houselog_event_local ("DISCOVERY", name, "DETECTED", "AT %s", url);
        isnew = 1;
    } else {
        if (housediscover_lapsed (DiscoveryLatest[byurl])) {
            int byservice = echttp_hash_find (&DiscoveryByService, name);
            if (byservice > 0) { // Re-detected after lapse
                DiscoveryFirstDetected[byservice] = now;
            }
        }
    }
    DiscoveryLatest[byurl] = now;

    if (isnew) {
        // Record one more instance of this service.
        int byservice = echttp_hash_add (&DiscoveryByService, strdup(name));
        if (byservice > 0) {
            DiscoveryUrl[byservice] = strdup(url);
            DiscoveryFirstDetected[byservice] = now;
        }
    }
    return isnew;
}

static void housediscover_service_response
                (void *origin, int status, char *data, int length) {

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, "service", "HTTP error %d", status);
        return;
    }
    if (!data) return; // Self protection.

    int count = housediscover_adjust_tokens (data);
    const char *error = echttp_json_parse (data, DiscoveryTokens, &count);
    if (error) {
        houselog_trace (HOUSE_FAILURE, "service", "JSON syntax error, %s", error);
        return;
    }
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, "service", "no data");
        return;
    }

    int host = echttp_json_search (DiscoveryTokens, ".host");
    int list = echttp_json_search (DiscoveryTokens, ".portal.redirect");
    if (host <= 0 || list <= 0) {
        houselog_trace (HOUSE_FAILURE, "service", "invalid data format");
        return;
    }
    int n = DiscoveryTokens[list].length;
    if (n == 0) return; // That is a normal case (no service on that server)
    if (n < 0 || n > count) {
        houselog_trace (HOUSE_FAILURE, "service", "invalid redirect data");
        return;
    }
    error = echttp_json_enumerate (DiscoveryTokens+list,
                                   DiscoveryInnerList, DiscoveryTokensAllocated);
    if (error) {
        houselog_trace (HOUSE_FAILURE, "service", "%s", error);
        return;
    }

    char *hostname = DiscoveryTokens[host].value.string;

    // Update the list of services.
    //
    DEBUG ("processing list of service providers\n");

    int i;
    for (i = 0; i < n; ++i) {
        ParserToken *inner = DiscoveryTokens + list + DiscoveryInnerList[i];
        if (inner->type != PARSER_OBJECT) {
            houselog_trace (HOUSE_FAILURE, "peers",
                            "unexpected type %d", inner->type);
            continue;
        }
        int service = echttp_json_search (inner, ".service");
        if (service <= 0) continue; // Not declared as a service.
        int path = echttp_json_search (inner, ".path");
        if (path <= 0) {
            houselog_trace (HOUSE_FAILURE, hostname,
                            "invalid redirect (no path)");
            DEBUG ("invalid redirect entry %d (no path) from %s\n",
                   i, hostname);
            continue;
        }

        char fullurl[256];
        snprintf (fullurl, sizeof(fullurl),
                  "http://%s%s", hostname, inner[path].value.string);

        const char *name = inner[service].value.string;

        housediscover_register (name, fullurl);
    }
}

static int housediscover_peers_iterator (int i, const char *name) {

    if (DiscoveryLatest[i]) {
        const char *url = DiscoveryUrl[i];
        const char *error = echttp_client ("GET", url);
        if (error) {
            DEBUG ("error on %s: %s.\n", url, error);
            houselog_trace (HOUSE_FAILURE, "peers", "%s: %s", url, error);
            DiscoveryLatest[i] = 0;
            return 0;
        }
        echttp_submit (0, 0, housediscover_service_response, 0);
        DEBUG ("service request %s submitted.\n", url);
    }
    return 0;
}

static void housediscover_peers_response (void *origin,
                                          int status, char *data, int length) {

    static time_t DiscoveryDetail = 0;

    int newportal = 0;
    int i;

    if (status != 200) {
        DEBUG ("HTTP error %d on /portal/peers request\n", status);
        houselog_trace (HOUSE_FAILURE, "peers", "HTTP error %d", status);
        return;
    }
    if (!data) return; // Self protection.

    time_t now = time(0);

    int count = housediscover_adjust_tokens (data);
    const char *error = echttp_json_parse (data, DiscoveryTokens, &count);
    if (error) {
        DEBUG ("JSON error on /portal/peers request: %s\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "JSON syntax error, %s", error);
        return;
    }
    if (count <= 0) {
        DEBUG ("JSON empty on /portal/peers request\n");
        houselog_trace (HOUSE_FAILURE, "peers", "no data");
        return;
    }
    int peers = echttp_json_search (DiscoveryTokens, ".portal.peers");
    int n = DiscoveryTokens[peers].length;
    if (n <= 0 || n > count) {
        DEBUG ("no peer data on portal request\n");
        houselog_trace (HOUSE_FAILURE, "peers", "empty zone data");
        return;
    }

    error = echttp_json_enumerate (DiscoveryTokens+peers,
                                   DiscoveryInnerList, DiscoveryTokensAllocated);
    if (error) {
        DEBUG ("no peers array on portal request: %s\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "%s", error);
        return;
    }

    DEBUG ("processing portals result.\n");

    for (i = 0; i < n; ++i) {
        ParserToken *inner = DiscoveryTokens + peers + DiscoveryInnerList[i];
        if (inner->type != PARSER_STRING) {
            houselog_trace (HOUSE_FAILURE, "peers",
                            "unexpected type %d", inner->type);
            continue;
        }
        char buffer[256];
        snprintf (buffer, sizeof(buffer),
                  "http://%s/portal/list", inner->value.string);
        if (housediscover_register ("portal", buffer)) {
             DEBUG ("new portal %s found.\n", inner->value.string);
             newportal = 1;
        }
    }

    // Now that we have updated our list of portal servers, query them.
    // Actually, do not query a new portal right away: give the services
    // a few seconds to declare themselves.
    //
    if (newportal) {
        // Not yet, force one new discovery 3 seconds from now.
        DiscoveryDetail = 0;
        DiscoveryRequest = now - 8;
    } else if (now >= DiscoveryDetail + DISCOVERY_SERVICE_INTERVAL) {
        echttp_hash_iterate (&DiscoveryByService,
                             "portal", housediscover_peers_iterator);
        DiscoveryDetail = now;
    }
}

void housediscover (time_t now) {

    if (!now) { // Manual discovery request (force discovery on next tick)
        DiscoveryRequest = 0;
        return;
    }
    if (now < DiscoveryRequest + DISCOVERY_PORTAL_INTERVAL) return;

    char url[100];
    snprintf (url, sizeof(url),
              "http://%s:%d/portal/peers", LocalPortalServer, LocalPortalPort);
    const char *error = echttp_client ("GET", url);
    if (error) {
        DEBUG ("cannot access %s: %s\n", url, error);
        houselog_trace (HOUSE_FAILURE, "peers",
                        "cannot access %s: %s", url, error);
    } else {
        echttp_submit (0, 0, housediscover_peers_response, 0);
        DEBUG ("request %s submitted\n", url);
    }
    DiscoveryRequest = now;
}

static time_t DiscoveryMostRecent = 0;

static int housediscover_changed_iterator (int i, const char *service) {
    if (DiscoveryFirstDetected[i] > DiscoveryMostRecent)
        DiscoveryMostRecent = DiscoveryFirstDetected[i];
    return 0;
}

int housediscover_changed (const char *service, time_t since) {
    DiscoveryMostRecent = 0;
    echttp_hash_iterate (&DiscoveryByService,
                         service, housediscover_changed_iterator);
    return DiscoveryMostRecent >= since;
}

static housediscover_consumer *DiscoveryConsumer = 0;
static void *DiscoveryConsumerContext = 0;

static int housediscovered_iterator (int i, const char *service) {

    if (housediscover_lapsed (DiscoveryLatest[i])) {
        return 0; // Too old, presumed dead.
    }

    if (DiscoveryConsumer)
        DiscoveryConsumer (service, DiscoveryConsumerContext, DiscoveryUrl[i]);
    return 0;
}

void housediscovered (const char *service,
                      void *context, housediscover_consumer *consumer) {

    DiscoveryConsumer = consumer;
    DiscoveryConsumerContext = context;
    echttp_hash_iterate (&DiscoveryByService,
                         service, housediscovered_iterator);
    DiscoveryConsumer = 0;
    DiscoveryConsumerContext = 0;
}

