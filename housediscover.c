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

static echttp_hash DiscoveryByUrl; // URL is a unique key.
static time_t DiscoveryTime[ECHTTP_MAX_SYMBOL];

static echttp_hash DiscoveryByService; // Service name is not unique.
static const char *DiscoveryUrl[ECHTTP_MAX_SYMBOL];


static time_t DiscoveryPendingTimestamp = 0;

#define DEBUG if (echttp_isdebug()) printf

void housediscover_initialize (int argc, const char **argv) {

    int i;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-portal-server=", argv[i], &LocalPortalServer))
            continue;
    }
    DEBUG ("local portal server: %s\n", LocalPortalServer);
}


static int housediscover_register (const char *name, const char *url) {

    int byurl = echttp_hash_find (&DiscoveryByUrl, url);
    int isnew = 0;

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
        houselog_event ("DISCOVERY", name, "DETECTED" "AT %s", url);
        isnew = 1;
    }
    DiscoveryTime[byurl] = time(0);

    if (isnew) {
        // Record one more instance of this service.
        int byservice = echttp_hash_add (&DiscoveryByService, strdup(name));
        if (byservice > 0) {
            DiscoveryUrl[byservice] = strdup(url);
        }
    }
    return isnew;
}

static void housediscover_service_response
                (void *origin, int status, char *data, int length) {

    const char *service = (char *)origin;
    ParserToken tokens[100];
    int count = 100;
    int innerlist[100];
    int i;

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, service, "HTTP error %d", status);
        return;
    }

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        houselog_trace (HOUSE_FAILURE, service, "JSON syntax error, %s", error);
        return;
    }
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, service, "no data");
        return;
    }

    int host = echttp_json_search (tokens, ".host");
    int list = echttp_json_search (tokens, ".portal.redirect");
    if (host <= 0 || list <= 0) {
        houselog_trace (HOUSE_FAILURE, service, "invalid data format");
        return;
    }
    int n = tokens[list].length;
    if (n == 0) return; // That is a normal case (no service on that server)
    if (n < 0 || n > 100) {
        houselog_trace (HOUSE_FAILURE, service, "invalid redirect data");
        return;
    }
    error = echttp_json_enumerate (tokens+list, innerlist);
    if (error) {
        houselog_trace (HOUSE_FAILURE, service, "%s", error);
        return;
    }

    char *hostname = tokens[host].value.string;

    // Update the list of services.
    //
    DEBUG ("processing list of service providers\n");

    for (i = 0; i < n; ++i) {
        ParserToken *inner = tokens + list + innerlist[i];
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

    const char *url = DiscoveryUrl[i];

    const char *error = echttp_client ("GET", url);
    if (error) {
        DEBUG ("error on %s: %s.\n", url, error);
        houselog_trace (HOUSE_FAILURE, url, "%s", error);
        return 0;
    }
    echttp_submit (0, 0, housediscover_service_response, 0);
    DEBUG ("service request %s submitted.\n", url);
    return 0;
}

static void housediscover_peers_response (void *origin,
                                          int status, char *data, int length) {

    ParserToken tokens[100];
    int innerlist[100];
    int count = 100;
    int i;

    time_t now = time(0);

    if (status != 200) {
        DEBUG ("HTTP error %d on /portal/peers request\n", status);
        houselog_trace (HOUSE_FAILURE, "peers", "HTTP error %d", status);
        return;
    }

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        DEBUG ("JSON error on /portal/peers request: %s\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "JSON syntax error, %s", error);
        return;
    }
    if (count <= 0) {
        DEBUG ("JSON empty %d on /portal/peers request\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "no data");
        return;
    }
    int peers = echttp_json_search (tokens, ".portal.peers");
    int n = tokens[peers].length;
    if (n <= 0 || n > 100) {
        DEBUG ("no data %d on portal request\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "empty zone data");
        return;
    }

    error = echttp_json_enumerate (tokens+peers, innerlist);
    if (error) {
        DEBUG ("no peers array %d on portal request\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "%s", error);
        return;
    }

    DEBUG ("processing portals result.\n");
    DiscoveryPendingTimestamp = time(0);

    for (i = 0; i < n; ++i) {
        ParserToken *inner = tokens + peers + innerlist[i];
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
        }
    }

    // Now that we have updated our list of portal servers, query them.
    //
    echttp_hash_iterate (&DiscoveryByService,
                         "portal", housediscover_peers_iterator);
}

void housediscover (time_t now) {

    static time_t DiscoveryRequest = 0;

    if (!now) { // Manual discovery request (force discovery on next tick)
        DiscoveryRequest = 0;
        return;
    }
    if (DiscoveryPendingTimestamp) {
        // Settled time: do discovery at slow speed (just a refresh).
        if (now < DiscoveryRequest + 600) return;
    } else {
        // Initialization time: fast speed until we get a response.
        if (now < DiscoveryRequest + 20) return;
    }

    char url[100];
    snprintf (url, sizeof(url), "http://%s/portal/peers", LocalPortalServer);
    const char *error = echttp_client ("GET", url);
    if (error) {
        DEBUG ("cannot access %s: %s\n", url, error);
        houselog_trace (HOUSE_FAILURE, "peers",
                        "cannot access %s: %s", url, error);
        return;
    }
    echttp_submit (0, 0, housediscover_peers_response, 0);
    DEBUG ("request %s submitted\n", url);

    DiscoveryRequest = now;
}

static housediscover_consumer *DiscoveryConsumer = 0;
static void *DiscoveryConsumerContext = 0;

static int housediscovered_iterator (int i, const char *service) {

    if (DiscoveryConsumer)
        DiscoveryConsumer (service, DiscoveryConsumerContext, DiscoveryUrl[i]);
    return 0;
}

void housediscovered (const char *service,
                      void *context, housediscover_consumer *consumer) {

    int i;

    DiscoveryConsumer = consumer;
    DiscoveryConsumerContext = context;
    echttp_hash_iterate (&DiscoveryByService,
                         service, housediscovered_iterator);
    DiscoveryConsumer = 0;
    DiscoveryConsumerContext = 0;
}

