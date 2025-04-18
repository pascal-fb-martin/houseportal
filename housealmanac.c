/* houseportal - A simple web portal for home servers
 *
 * Copyright 2025, Pascal Martin
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
 * housealmanac.c - Client interface with the almanac services.
 *
 * SYNOPSYS:
 *
 * This module handles detection of, and communication with, almanac
 * services:
 * - Run periodic discoveries to find which servers provide almanac data.
 * - Choose the highest priority source.
 * - Request Almanac data daily.
 *
 * This module caches the current almanac data, so that the process for
 * fetching the data is fully asynchronous.
 *
 * int housealmanac_ready (void);
 *
 *    Return 1 if almanac data was fetched from at least one service in
 *    the last 24 hours.
 *    The purpose is to allow the caller to delay processing until at least
 *    one almanac service has been detected.
 *
 * time_t housealmanac_sunset (void);
 * time_t housealmanac_sunrise (void);
 *
 *    Return the sunset or sunrise time for the recent or upcoming night.
 *    The latest almanac data is typically queried during daytime, so
 *    the sunset and sunrise times returned in night time are for the
 *    current night. The logic used here is: if it is not night time,
 *    then it is daytime.
 *
 * void housealmanac_background (time_t now);
 *
 *    The periodic function that detects the almanac services.
 *
 * int housealmanac_status (char *buffer, int size);
 *
 *    Return a JSON dump of the current almanac data.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"

#include "housealmanac.h"

#define DEBUG if (echttp_isdebug()) printf

static int    SourcePriority = 0;
static char   SourceUri[128];
static time_t SunSet = 0;
static time_t SunRise = 0;

// Additional information, not really used here:
static int    HouseGpsFix = 0;
static double HouseLatitude = 0.0;
static double HouseLongitude = 0.0;
static char   HouseTimeZone[128];

int housealmanac_ready (void) {
    return SunSet > 0; // Even if the data has expired.
}

const time_t housealmanac_sunset (void) {
    return SunSet;
}

const time_t housealmanac_sunrise (void) {
    return SunRise;
}

static ParserToken *housealmanac_prepare (int count) {

    static ParserToken *EventTokens = 0;
    static int EventTokensAllocated = 0;

    if (count > EventTokensAllocated) {
        int need = EventTokensAllocated = count + 128;
        EventTokens = realloc (EventTokens, need*sizeof(ParserToken));
    }
    return EventTokens;
}

static void housealmanac_update (const char *provider,
                                      char *data, int length) {

   time_t now = time(0);

   if (now > SunRise) SourcePriority = 0; // Data is past its prime.

   int count = echttp_json_estimate(data);
   ParserToken *tokens = housealmanac_prepare (count);

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

   int index = echttp_json_search (tokens, ".almanac.priority");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no priority data");
       return;
   }
   int priority = tokens[index].value.integer;
   if (priority <= SourcePriority) return; // Lower quality.

   index = echttp_json_search (tokens, ".almanac.sunrise");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no sunrise data");
       return;
   }
   if (tokens[index].value.integer < SunRise) return; // Older than existing.
   SunRise = tokens[index].value.integer;

   index = echttp_json_search (tokens, ".almanac.sunset");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no sunset data");
       return;
   }
   SunSet = tokens[index].value.integer;

   SourcePriority = priority; // Accept the new data.
   snprintf (SourceUri, sizeof(SourceUri), "%s", provider);

   // If this almanac server has location info, remember it.
   index = echttp_json_search (tokens, ".location.timezone");
   if (index > 0) {
       const char *value = tokens[index].value.string;
       if (strcmp (value, HouseTimeZone)) {
           snprintf (HouseTimeZone, sizeof(HouseTimeZone), "%s", value);
       }
   }
   int lat = echttp_json_search (tokens, ".location.lat");
   int lng = echttp_json_search (tokens, ".location.long");
   if ((lat > 0) && (lng > 0)) {
       HouseLatitude = tokens[lat].value.real;
       HouseLongitude = tokens[lng].value.real;
       HouseGpsFix = 1;
   }
}

static void housealmanac_discovered
               (void *origin, int status, char *data, int length) {

   const char *provider = (const char *) origin;

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housealmanac_discovered, origin);
       return;
   }

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, provider, "HTTP error %d", status);
       return;
   }

   housealmanac_update (provider, data, length);
}

static void housealmanac_scan_server
                (const char *service, void *context, const char *provider) {

    char url[256];

    snprintf (url, sizeof(url), "%s/nextnight", provider);

    DEBUG ("Attempting query at %s\n", url);
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, provider, "%s", error);
        return;
    }
    echttp_submit (0, 0, housealmanac_discovered, (void *)provider);
}

void housealmanac_background (time_t now) {

    static time_t latestdiscovery = 0;

    if (!now) { // This is a manual reset (force a discovery refresh soon)
        latestdiscovery = 0;
        return;
    }

    // If any new service was detected, force a scan now.
    //
    if ((latestdiscovery > 0) &&
        housediscover_changed ("almanac", latestdiscovery)) {
        latestdiscovery = 0;
    }

    // If the almanac data is unknown or has expired, scan every 10 seconds.
    // Otherwise, scan every few minutes.
    //
    time_t deadline = latestdiscovery + ((SunRise <= now)?10:300);
    if (now <= deadline) return;
    latestdiscovery = now;

    DEBUG ("Proceeding with almanac discovery\n");
    housediscovered ("almanac", 0, housealmanac_scan_server);
}

int housealmanac_status (char *buffer, int size) {

    int cursor;

    if (SourcePriority <= 0) return 0;

    cursor = snprintf (buffer, size,
                       ",\"almanac\":{\"priority\":%d,\"provider\":\"%s\""
                       ",\"sunset\":%lld,\"sunrise\":%lld}",
                       SourcePriority, SourceUri,
                       (long long)SunSet, (long long)SunRise);

    if (HouseTimeZone[0] || HouseGpsFix) {
        cursor += snprintf (buffer+cursor, size-cursor, ",\"location\":{");

        const char *sep = "";
        if (HouseTimeZone[0]) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s\"timezone\":\"%s\"", sep, HouseTimeZone);
            sep = ",";
        }
        if (HouseGpsFix) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s\"lat\":%1.8f,\"long\":%1.8f",
                                sep, HouseLatitude, HouseLongitude);
            sep = ",";
        }
        cursor += snprintf (buffer+cursor, size-cursor, "}");
    }

    if (cursor >= size) {
        houselog_trace (HOUSE_FAILURE, "STATUS",
                        "BUFFER TOO SMALL (NEED %d bytes)", cursor);
        return 0;
    }
    return cursor;
}

