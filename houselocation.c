/* houseportal - A simple web portal for home servers
 *
 * Copyright 2026, Pascal Martin
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
 * houselocation.c - Retrieve the location of the house.
 *
 * SYNOPSYS:
 *
 * This module handles detection of, and communication with, clock
 * services:
 * - Run periodic discoveries to find which servers provide clock data.
 * - Query these servers and capture GPS position.
 *
 * This module caches the current location data, so that the process for
 * fetching the data is fully asynchronous.
 *
 * int houselocation_ready (void);
 *
 *    Return 1 if location data is available.
 *    The purpose is to delay processing that requires the location data.
 *
 * double houselocation_lat (void);
 * double houselocation_long (void);
 *
 *    Return the house latitude and longitude coordinates.
 *
 * void houselocation_initialize (int argc, const char **argv);
 *
 *    Initialize this module. This accepts the -latitude=N and -lontitude=N
 *    options, which set a fixed house location when both are provided.
 *
 * void houselocation_enable (int enable);
 *
 *    Enable or disable periodic house location discovery.
 *
 *    If a fixed latitude/longitude location was set (see above),
 *    periodic house location discovery is always disabled regardless.
 *    Otherwise periodic location discovery is enabled by default.
 *
 * void houselocation_background (time_t now);
 *
 *    The periodic function that detects the clock services.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"

#include "houselocation.h"

#define DEBUG if (echttp_isdebug()) printf

static int    HouseLocationEnabled = 0;
static int    HouseGpsFix = 0;
static double HouseLatitude = 0.0;
static double HouseLongitude = 0.0;

int houselocation_ready (void) {
    return (HouseLocationEnabled && (HouseGpsFix > 0));
}

double houselocation_lat (void) {
    return HouseLatitude;
}

double houselocation_long (void) {
    return HouseLongitude;
}

static ParserToken *houselocation_prepare (int count) {

    static ParserToken *EventTokens = 0;
    static int EventTokensAllocated = 0;

    if (count > EventTokensAllocated) {
        int need = EventTokensAllocated = count + 128;
        EventTokens = realloc (EventTokens, need*sizeof(ParserToken));
    }
    return EventTokens;
}

static void houselocation_update (const char *provider,
                                  char *data, int length) {

   int count = echttp_json_estimate(data);
   ParserToken *tokens = houselocation_prepare (count);

   const char *error = echttp_json_parse (data, tokens, &count);
   if (error) {
       houselog_trace
           (HOUSE_FAILURE, provider, "JSON syntax error, %s", error);
       return;
   }
   if (count <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no data");
       return;
   }

   int index = echttp_json_search (tokens, ".clock.gps.fix");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no GPS fix indicator");
       return;
   }
   if (!tokens[index].value.boolean) return;

   index = echttp_json_search (tokens, ".clock.gps.latitude");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no latitude data");
       return;
   }
   double latitude = tokens[index].value.real;

   index = echttp_json_search (tokens, ".clock.gps.longitude");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no longitude data");
       return;
   }
   HouseLongitude = tokens[index].value.real;
   HouseLatitude = latitude;
   HouseGpsFix = 1;
   DEBUG ("Obtained house location: Lat %f, Long %f\n", HouseLatitude, HouseLongitude);
}

static void houselocation_discovered
               (void *origin, int status, char *data, int length) {

   const char *provider = (const char *) origin;

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, houselocation_discovered, origin);
       return;
   }

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, provider, "HTTP error %d", status);
       return;
   }

   houselocation_update (provider, data, length);
}

static void houselocation_scan
                (const char *service, void *context, const char *provider) {

    char url[256];

    snprintf (url, sizeof(url), "%s/status", provider);

    DEBUG ("Attempting query at %s\n", url);
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, provider, "%s", error);
        return;
    }
    echttp_submit (0, 0, houselocation_discovered, (void *)provider);
}

void houselocation_initialize (int argc, const char **argv) {

    int i;
    const char *value;
    char *end;
    for (i = 1; i < argc; ++i) {
         if (echttp_option_match("-latitude=", argv[i], &value)) {
             HouseLatitude = strtod (value, &end);
             HouseGpsFix += 1;
         } else if (echttp_option_match("-longitude=", argv[i], &value)) {
             HouseLongitude = strtod (value, &end);
             HouseGpsFix += 1;
         }
    }
    HouseLocationEnabled = 1;
}

void houselocation_enable (int enable) {
    if (HouseGpsFix < 2) HouseLocationEnabled = enable;
}

void houselocation_background (time_t now) {

    static time_t latestdiscovery = 0;

    if (HouseGpsFix >= 2) return; // User supplied fixed coordinates.
    if (!HouseLocationEnabled) return;

    // If any new service was detected, force a scan now.
    //
    if ((latestdiscovery > 0) &&
        housediscover_changed ("clock", latestdiscovery)) {
        latestdiscovery = 0;
    }

    if (HouseGpsFix) {
        if (now < latestdiscovery + 300) return; // Handle the RV use case..
    } else {
        if (now < latestdiscovery + 5) return; // Go wild until a fix is found
    }
    latestdiscovery = now;

    DEBUG ("Proceeding with clock service discovery\n");
    housediscovered ("clock", 0, houselocation_scan);
}

