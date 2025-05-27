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
 * There are two variants of almanac data: tonight or today. Which one to
 * use depends on the goal. The "tonight" set is specifically designed to
 * reliably answer the question "is it night time now" even while crossing
 * midnight. The two sets can be accessed concurrently in the same application.
 *
 * The "tonight" set is defined as the first upcoming sunrise combined with
 * the previous sunset. The "today" set is the more traditional almanac data:
 * the sunrise and sunset times for the current day.
 *
 * A set is only queried if requested: the application should call the
 * related "ready" method at least once, typically during initialization.
 *
 * This module caches the current almanac data, so that the process for
 * fetching the data is fully asynchronous.
 *
 * int housealmanac_tonight_ready (void);
 * int housealmanac_today_ready (void);
 *
 *    Return 1 if the almanac data was fetched from at least one service in
 *    the last 24 hours.
 *    The purpose is to allow the caller to delay processing until at least
 *    one almanac service has been detected.
 *
 * time_t housealmanac_tonight_sunset (void);
 * time_t housealmanac_tonight_sunrise (void);
 *
 *    Return the sunset or sunrise time for the current or upcoming night.
 *
 * time_t housealmanac_today_sunset (void);
 * time_t housealmanac_today_sunrise (void);
 *
 *    Return the sunset or sunrise time for the current day.
 *
 * const char *housealmanac_tonight_provider (void);
 * int         housealmanac_today_priority (void);
 *
 *    Return information about the service that provided the almanac data.
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

struct AlmanacDataBase {
    char active;
    char gps;
    short priority;
    char timezone[128];
    char source[128];
    double latitude;
    double longitude;
    time_t sunset;
    time_t sunrise;
};

static struct AlmanacDataBase Tonight = {0};
static struct AlmanacDataBase Today = {0};

static int housealmanac_ready (struct AlmanacDataBase *db) {
    db->active = 1;
    return db->sunset > 0; // Even if the data has expired.
}

static const time_t housealmanac_sunset (struct AlmanacDataBase *db) {
    db->active = 1;
    return db->sunset;
}

static const time_t housealmanac_sunrise (struct AlmanacDataBase *db) {
    db->active = 1;
    return db->sunrise;
}

static const char *housealmanac_provider (struct AlmanacDataBase *db) {
    db->active = 1;
    return db->source;
}

int housealmanac_priority (struct AlmanacDataBase *db) {
    db->active = 1;
    return db->priority;
}

int housealmanac_tonight_ready (void) {
    return housealmanac_ready (&Tonight);
}

const time_t housealmanac_tonight_sunset (void) {
    return housealmanac_sunset (&Tonight);
}

const time_t housealmanac_tonight_sunrise (void) {
    return housealmanac_sunrise (&Tonight);
}

const char *housealmanac_tonight_provider (void) {
    return housealmanac_provider (&Tonight);
}

int housealmanac_tonight_priority (void) {
    return housealmanac_priority (&Tonight);
}

int housealmanac_today_ready (void) {
    return housealmanac_ready (&Today);
}

const time_t housealmanac_today_sunset (void) {
    return housealmanac_sunset (&Today);
}

const time_t housealmanac_today_sunrise (void) {
    return housealmanac_sunrise (&Today);
}

const char *housealmanac_today_provider (void) {
    return housealmanac_provider (&Today);
}

int housealmanac_today_priority (void) {
    return housealmanac_priority (&Today);
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

   if (now > Tonight.sunrise) Tonight.priority = 0; // Data is obsolete.
   if (now > Today.sunset + (12*60*60)) Today.priority = 0; // Data is obsolete.

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
   short priority = (short)tokens[index].value.integer;

   index = echttp_json_search (tokens, ".almanac.sunrise");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no sunrise data");
       return;
   }
   time_t sunrise = tokens[index].value.integer;

   index = echttp_json_search (tokens, ".almanac.sunset");
   if (index <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no sunset data");
       return;
   }
   time_t sunset = tokens[index].value.integer;

   struct AlmanacDataBase *db = &Tonight;
   if (sunset > sunrise) { // This is a "today" request.
       db = &Today;
   }
   if (priority < db->priority) return; // Ignore lower quality data.

   db->priority = priority; // Accept the new data.;
   db->sunset = sunset;
   db->sunrise = sunrise;

   snprintf (db->source, sizeof(db->source), "%s", provider);

   // If this almanac server has location info, remember it.
   index = echttp_json_search (tokens, ".location.timezone");
   if (index > 0) {
       const char *value = tokens[index].value.string;
       if (strcmp (value, db->timezone)) {
           snprintf (db->timezone, sizeof(db->timezone), "%s", value);
       }
   }
   int lat = echttp_json_search (tokens, ".location.lat");
   int lng = echttp_json_search (tokens, ".location.long");
   if ((lat > 0) && (lng > 0)) {
       db->latitude = tokens[lat].value.real;
       db->longitude = tokens[lng].value.real;
       db->gps = 1;
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

    const char *day = (const char *)context;
    char url[256];

    snprintf (url, sizeof(url), "%s/%s", provider, day);

    DEBUG ("Attempting almanac query at %s\n", url);
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

    // If any almanac data is unknown or has expired, scan every 10 seconds.
    // Otherwise, scan every few minutes.
    //
    time_t deadline = latestdiscovery + 300;
    if (Tonight.active && (Tonight.sunrise <= now))
        deadline = latestdiscovery + 10;
    if (Today.active && (Today.sunset + (12*60*60) <= now))
        deadline = latestdiscovery + 10;
    if (now <= deadline) return;
    latestdiscovery = now;

    DEBUG ("Proceeding with almanac discovery\n");
    if (Tonight.active)
       housediscovered ("almanac", "tonight", housealmanac_scan_server);
    if (Today.active)
       housediscovered ("almanac", "today", housealmanac_scan_server);
}

int housealmanac_status (char *buffer, int size) {

    int cursor;

    struct AlmanacDataBase *db;

    if (Tonight.active) db = &Tonight;
    else if (Today.active) db = &Today;
    else return 0; // No status.

    if (db->priority <= 0) return 0;

    cursor = snprintf (buffer, size,
                       ",\"almanac\":{\"priority\":%d,\"provider\":\"%s\""
                       ",\"sunset\":%lld,\"sunrise\":%lld}",
                       db->priority, db->source,
                       (long long)db->sunset, (long long)db->sunrise);

    if (db->timezone[0] || db->gps) {
        cursor += snprintf (buffer+cursor, size-cursor, ",\"location\":{");

        const char *sep = "";
        if (db->timezone[0]) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s\"timezone\":\"%s\"", sep, db->timezone);
            sep = ",";
        }
        if (db->gps) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s\"lat\":%1.8f,\"long\":%1.8f",
                                sep, db->latitude, db->longitude);
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

