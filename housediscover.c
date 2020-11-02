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
 * void housediscover (const char *service);
 *
 *    Search for all providers for the specified service on the network.
 *    This is an asynchronous process: the actual discovery will take place
 *    later, when receiving responses. One way to use this function is to
 *    call it periodically, or a minute before the result is needed.
 *
 *    Note that doing a discovery is expensive (it involves multiple HTTP
 *    queries), so doing it only when needed might be a good idea..
 *
 * void housediscovered (const char *service, void *context,
 *                       housediscover_consumer *consumer);
 *
 *    Retrieve the result of the latest discovery. This is a classic iterator
 *    design: the consumer function is called for each matching item found.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "echttp.h"
#include "echttp_json.h"
#include "echttp_catalog.h"

#include "houselog.h"
#include "housediscover.h"

static const char *LocalPortalServer = "localhost";

static echttp_catalog DiscoveryCache;

static time_t DiscoveryTimestamp = 0;

#define DEBUG if (echttp_isdebug()) printf

void housediscover_initialize (int argc, const char **argv) {

    int i;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-portal-server=", argv[i], &LocalPortalServer))
            continue;
    }
    DEBUG ("local portal server: %s\n", LocalPortalServer);
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
    int list = echttp_json_search (tokens, ".portal.service.url");
    int n = tokens[list].length;
    if (n <= 0 || n > 100) {
        houselog_trace (HOUSE_FAILURE, service, "empty zone data");
        return;
    }
    error = echttp_json_enumerate (tokens+list, innerlist);
    if (error) {
        houselog_trace (HOUSE_FAILURE, service, "%s", error);
        return;
    }

    // Update the list of services.
    //
    DEBUG ("processing list of providers for %s.\n", service);
    time_t now = time(0);

    for (i = 0; i < n; ++i) {
        ParserToken *inner = tokens + list + innerlist[i];
        if (inner->type != PARSER_STRING) {
            houselog_trace (HOUSE_FAILURE, "peers",
                            "unexpected type %d", inner->type);
            continue;
        }
        const char *old =
            echttp_catalog_refresh (&DiscoveryCache,
                                    strdup(inner->value.string), service, now);
        if (old) free((char *)old);
    }
}

static void housediscover_peers_query (const char *service) {

    int i;

    for (i = 1; i <= DiscoveryCache.count; ++i) {
        if (DiscoveryCache.item[i].timestamp < DiscoveryTimestamp) continue;
        if (strcmp (DiscoveryCache.item[i].value, "portal")) continue;

        char url[300];
        snprintf (url, sizeof(url),
                  "%s?name=%s", DiscoveryCache.item[i].name, service);
        const char *error = echttp_client ("GET", url);
        if (error) {
            DEBUG ("error on %s: %s.\n", url, error);
            houselog_trace (HOUSE_FAILURE, DiscoveryCache.item[i].name, "%s", error);
            continue;
        }
        echttp_submit (0, 0, housediscover_service_response, (void *)service);
        DEBUG ("service request %s submited.\n", url);
    }
}

static void housediscover_peers_response (void *origin,
                                          int status, char *data, int length) {

    ParserToken tokens[100];
    int innerlist[100];
    int count = 100;
    int i;

    time_t now = time(0);

    if (status != 200) {
        DEBUG ("HTTP error %d on peers request\n", status);
        houselog_trace (HOUSE_FAILURE, "peers", "HTTP error %d", status);
        return;
    }

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        DEBUG ("JSON error on peers request: %s\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "JSON syntax error, %s", error);
        return;
    }
    if (count <= 0) {
        DEBUG ("JSON empty %d on peers request\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "no data");
        return;
    }
    int peers = echttp_json_search (tokens, ".portal.peers");
    int n = tokens[peers].length;
    if (n <= 0 || n > 100) {
        DEBUG ("no data %d on peers request\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "empty zone data");
        return;
    }

    error = echttp_json_enumerate (tokens+peers, innerlist);
    if (error) {
        DEBUG ("no peers array %d on peers request\n", error);
        houselog_trace (HOUSE_FAILURE, "peers", "%s", error);
        return;
    }

    DEBUG ("processing peers result.\n");
    DiscoveryTimestamp = time(0);

    for (i = 0; i < n; ++i) {
        ParserToken *inner = tokens + peers + innerlist[i];
        if (inner->type != PARSER_STRING) {
            houselog_trace (HOUSE_FAILURE, "peers",
                            "unexpected type %d", inner->type);
            continue;
        }
        char url[256];
        snprintf (url, sizeof(url),
                  "http://%s/portal/service", inner->value.string);
        const char *old =
            echttp_catalog_refresh (&DiscoveryCache,
                                    strdup(url), "portal",
                                    DiscoveryTimestamp);
        if (old) free((char *)old);
        DEBUG ("peer %s found.\n", url);
    }

    // Now that we have received a new list of portal servers, query them.
    housediscover_peers_query ((const char *)origin);
}

void housediscover (const char *service) {

    static time_t DiscoveryRequest = 0;
    time_t now = time(0);

    if (DiscoveryRequest + 60 < now) {

        // The last query for portal servers is old enough: renew the list.
        char url[100];
        snprintf (url, sizeof(url),
                  "http://%s/portal/peers", LocalPortalServer);
        const char *error = echttp_client ("GET", url);
        if (error) {
            DEBUG ("cannot access %s: %s\n", url, error);
            houselog_trace (HOUSE_FAILURE, "peers",
                            "cannot access %s: %s", url, error);
            return;
        }
        echttp_submit (0, 0, housediscover_peers_response, (void *)service);
        DEBUG ("peer request %s submited\n", url);

        DiscoveryRequest = now;

    } else {

        // Reuse the list of portal servers we already have.
        housediscover_peers_query (service);
    }
}

void housediscovered (const char *service, void *context,
                      housediscover_consumer *consumer) {

    int i;

    for (i = 1; i <= DiscoveryCache.count; ++i) {
        if (DiscoveryCache.item[i].timestamp < DiscoveryTimestamp) continue;
        if (strcmp (DiscoveryCache.item[i].value, service)) continue;

        consumer (service, context, DiscoveryCache.item[i].name);
    }
}

