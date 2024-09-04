/* houseportal - A simple web portal for home servers
 *
 * Copyright 2020, Pascal Martin
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
 * housedepositor.c - The generic client side of the HouseDepot service.
 *
 * SYNOPSYS:
 *
 * This module handles discovering, updating and loading files from HouseDepot
 * services on the network. This module is responsible for finding the most
 * recent copy of the "current" file, and detect subsequent changes.
 *
 * The module only considers the "current" revision of the file. Since it
 * queries multiple HouseDepot services, it only indicates a change if
 * all the HouseDepot services agree that the known revision is no longer
 * current, or if a more recent current file is available. In other words,
 * a file revision's upgrade occurs as soon as it appears in one HouseDepot
 * repository, while a downgrade only occurs if the rollback happened in
 * all HouseDepot repositories.
 *
 * It also maintains a local cache of all files that are stored in
 * HouseDepot's repositories, so that the start order of the House services
 * does not impact their availability. It does not matter if the cached file's
 * date is more recent: it must simply match. This is because the "current"
 * tag may have been moved to an older revision.
 *
 * void housedepositor_initialize (int argc, const char **argv);
 * 
 *    Recover these service configuration parameters:
 *       -group=*
 *
 * typedef void housedepositor_listener (const char *name, time_t timestamp,
 *                                       const char *data, int length);
 * 
 * void housedepositor_subscribe (const char *repository,
 *                                const char *name,
 *                                housedepositor_listener *listener);
 * 
 *    Subscribe for the specified file (on all HouseDepot services).
 *    It is legal to scan multiple files from multiple repositories.
 *    This does not download the file immediately, this is used to
 *    build a list to scan later.
 * 
 * int housedepositor_put (const char *repository,
 *                         const char *name,
 *                         const char *data, int size);
 *
 *    Update the named file in all discovered HouseDepot repositories.
 *    (The repository and name are typically constants, while the group is
 *    provided as a command line parameter.)
 *
 * void housedepositor_periodic (void);
 *
 *    Background updates.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"
#include "housedepositor.h"

#define DEBUG if (echttp_isdebug()) printf

static const char *DepotGroup = "home";
static int DepotScanPending = 0;

typedef struct {
    char uri[256];
    char *base;
    housedepositor_listener *listener;
    int refreshing;
    time_t active;   // The timestamp of the file that is used now.
    time_t detected; // The most recent timestamp detected.
    char host[128];  // The host that holds the most recent timestamp.
    time_t hostalive; // The last time this host responded.
} DepotCacheEntry;

#define MAX_CACHE 256

static DepotCacheEntry DepotCache[MAX_CACHE];
static int             DepotCacheCount = 0;

#define MAX_SOURCE 64

static char *DepotRepositories[MAX_SOURCE] = {0};


void housedepositor_initialize (int argc, const char **argv) {

    int i;
    for (i = 1; i < argc; i++) {
        if (echttp_option_match("-group=", argv[i], &DepotGroup))
            continue;
    }
}

static int housedepositor_search (const char *name) {
    int i;
    for (i = 0; i < DepotCacheCount; i++) {
        if (!strcmp(DepotCache[i].uri, name)) return i;
    }
    return -1;
}

void housedepositor_subscribe (const char *repository,
                               const char *name,
                               housedepositor_listener *listener) {
    
    if (DepotCacheCount >= MAX_CACHE) {
        houselog_trace (HOUSE_FAILURE, name,
                        "Registration cache full (file %s)", name);
        return;
    }
    
    char path[1024];
    snprintf (path, sizeof(path),
              "/depot/%s/%s/%s", repository, DepotGroup, name);

    DEBUG ("subscribe to %s\n", path);

    int i = housedepositor_search(path);
    if (i >= 0) {
        if (listener != DepotCache[i].listener) {
           houselog_trace (HOUSE_FAILURE, name,
                           "Registration conflict (repository %s)", repository);
        }
        return;
    }
    i = DepotCacheCount++;
    snprintf (DepotCache[i].uri, sizeof(DepotCache[0].uri), "%s", path);
    DepotCache[i].base = strrchr(DepotCache[i].uri, '/');
    if (DepotCache[i].base)
        DepotCache[i].base += 1;
    else
        DepotCache[i].base = DepotCache[i].uri;

    DepotCache[i].listener = listener;
    DepotCache[i].active = 0;
    DepotCache[i].detected = 0;
    DepotCache[i].refreshing = 0;

    for (i = 0; i < MAX_SOURCE; i++) {
        if (!(DepotRepositories[i])) {
            DepotRepositories[i] = strdup(repository);
            DEBUG ("Added repository %s\n", repository);
            break; // Just added.
        }
        if (!strcmp (DepotRepositories[i], repository))
            break; // Already present.
    }
}


typedef struct {
    char *path;
    int pending;
    int length;
    const char *data;
    time_t timestamp;
} HouseDepositorPutContext;

static void housedepositor_put_response
               (void *context, int status, char *data, int length) {

    HouseDepositorPutContext *request =
        (HouseDepositorPutContext *)context;

   status = echttp_redirected("PUT");
   if (!status){
       echttp_submit (request->data, request->length,
                      housedepositor_put_response, context);
       return;
   }
   
   DEBUG ("response to put of %s: %s\n", request->path, (length > 0)?data:"");

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, request->path, "HTTP code %d", status);
   }

   if ((--request->pending) <= 0) { // Last response.
       free (request->path);
       free (request);
   }
}

static void housedepositor_put_iterator
               (const char *service, void *context, const char *provider) {

    HouseDepositorPutContext *request =
        (HouseDepositorPutContext *)context;
    
    char url[1024];
    
    snprintf (url, sizeof(url), "%s/%s?time=%lld",
              provider, request->path, request->timestamp);
    const char *error = echttp_client ("PUT", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, service,
                        "cannot create socket for %s, %s", url, error);
        return;
    }
    DEBUG ("PUT %s : %s\n", url, request->data);
    request->pending += 1;
    echttp_submit (request->data, request->length,
                   housedepositor_put_response, context);
}

int housedepositor_put (const char *repository,
                        const char *name,
                        const char *data, int size) {
    
    char path[1024];
    time_t now = time(0);

    snprintf (path, sizeof(path),
              "%s/%s/%s", repository, DepotGroup, name);

    HouseDepositorPutContext *request =
        (HouseDepositorPutContext *) malloc (sizeof(HouseDepositorPutContext));
    
    request->pending = 0;
    request->path = strdup(path);
    request->length = size;
    request->data = data;
    request->timestamp = now;

    housediscovered ("depot", request, housedepositor_put_iterator);

    int cached = housedepositor_search(path);
    if (cached >= 0) {
        DepotCache[cached].active = now;
    }
}


static void housedepositor_get_response
               (void *context, int status, char *data, int length) {

    status = echttp_redirected("GET");
    if (!status){
        echttp_submit (0, 0, housedepositor_get_response, context);
        return;
    }
   
    DepotCacheEntry *cache = (DepotCacheEntry *)context;
    cache->refreshing = 0;
    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, cache->uri, "HTTP code %d", status);
        return;
    }
    
    DEBUG ("response to get %s: %s\n", cache->uri, data);

    if (cache->listener) {
        cache->listener(cache->base, cache->detected, data, length);
        cache->active = cache->detected;
    }
}

static void housedepositor_get (DepotCacheEntry *cache) {
    
    char url[1024];
    snprintf (url, sizeof(url), "http://%s%s", cache->host, cache->uri);
              
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, cache->base,
                        "cannot create socket for %s: %s", url, error);
        return;
    }
    DEBUG ("GET %s\n", url);
    echttp_submit (0, 0, housedepositor_get_response, (void *)cache);
}

static void housedepositor_refresh (void) {
    
    int i;
    for (i = 0; i < MAX_CACHE; i++) {
        if (!DepotCache[i].detected) continue;
        if (DepotCache[i].refreshing) continue;
        if (DepotCache[i].detected != DepotCache[i].active) {
            DEBUG ("Need to refresh %s (%d != %d)\n",
                   DepotCache[i].uri,
                   (int)(DepotCache[i].detected), (int)(DepotCache[i].active));
            housedepositor_get(&(DepotCache[i]));
            DepotCache[i].refreshing = 1;
        }
    }
}


static void housedepositor_scan_response
               (void *context, int status, char *data, int length) {

    time_t now = time(0);
    const char *repository = (char *)context;

    status = echttp_redirected("GET");
    if (!status){
        echttp_submit (0, 0, housedepositor_scan_response, context);
        return;
    }
    DepotScanPending -= 1;
    if (DepotScanPending <= 0) DEBUG ("Scan of HouseDepot services completed\n");
    
    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, repository, "HTTP code %d", status);
        return;
    }
   
    DEBUG ("response to scan of %s: %s\n", repository, data);

    ParserToken tokens[MAX_CACHE*4];
    int count = MAX_CACHE*4;
   
    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, repository, "JSON syntax error: %s", error);
        return;
    }
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, repository, "no data");
        return;
    }
    
    int host = echttp_json_search (tokens, ".host");
    if (host <= 0) {
        houselog_trace (HOUSE_FAILURE, repository, "no host");
        return;
    }
    int files = echttp_json_search (tokens, ".files");
    if (files <= 0) {
        houselog_trace (HOUSE_FAILURE, repository, "no file");
        return;
    }
    int n = tokens[files].length;
    if (n <= 0) return;

    int innerlist[256];
    error = echttp_json_enumerate (tokens+files, innerlist);
    if (error) {
        houselog_trace (HOUSE_FAILURE, repository, "bad file list");
        return;
    }

    int i;
    for (i = 0; i < n; i++) {
        ParserToken *inner = tokens + files + innerlist[i];
        int filename = echttp_json_search (inner, ".name");
        int filetime = echttp_json_search (inner, ".time");
        if (filename <= 0 || filetime <= 0) continue;
        int cached = housedepositor_search (inner[filename].value.string);
        if (cached < 0) continue;
        DEBUG ("Found %s at %s\n", inner[filename].value.string,
                                   tokens[host].value.string);
        time_t timestamp = (time_t) (inner[filetime].value.integer);

        if (! DepotCache[cached].active) {
            // We keep searching for the most recent revision as long as
            // the configuration item has not been activated yet.
            //
            if (DepotCache[cached].detected < timestamp) {
                snprintf(DepotCache[cached].host, sizeof(DepotCache[0].host),
                         "%s", tokens[host].value.string);
                DepotCache[cached].detected = timestamp;
                DepotCache[cached].hostalive = now;
            }
        } else if (!strcmp(DepotCache[cached].host, tokens[host].value.string)) {
            // If the configuration was already activated, follow the chosen
            // server.
            //
            DepotCache[cached].detected = timestamp;
            DepotCache[cached].hostalive = now;
        } else if (DepotCache[cached].hostalive < now - 180) {
            // If the chosen server is no longer responding, replace it.
            //
            snprintf(DepotCache[cached].host, sizeof(DepotCache[0].host),
                     "%s", tokens[host].value.string);
            DepotCache[cached].detected = timestamp;
            DepotCache[cached].hostalive = now;
        }
    }
}

static void housedepositor_scan_iterator
                (const char *service, void *context, const char *provider) {

    const char *repository = (const char *)context;
    
    char url[1024];
    snprintf (url, sizeof(url),
              "%s/%s/%s/all", provider, repository, DepotGroup);

    DEBUG ("GET %s\n", url);
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, repository,
                        "cannot create socket for %s, %s", url, error);
        return;
    }
    DEBUG ("GET %s \n", url);
    DepotScanPending += 1;
    echttp_submit (0, 0, housedepositor_scan_response, context);
}


void housedepositor_periodic (time_t now) {
    
    static time_t DepotLastScan = 0;
    
    if (DepotScanPending > 0) return;
    
    if (now < DepotLastScan + 60) {
        housedepositor_refresh();
        return;
    }

    DEBUG ("Starting to scan all depot services\n");
    int i;
    for (i = 0; i < MAX_CACHE; i++) {
        DepotCache[i].detected = 0;
    }
    DepotScanPending = 0;

    for (i = 0; i < MAX_SOURCE; i++) {
        if (!DepotRepositories[i]) break;
        housediscovered
            ("depot", (void *)(DepotRepositories[i]), housedepositor_scan_iterator);
    }

    // Only delay the next scan if we had services to scan.
    if (DepotScanPending) DepotLastScan = now;
}

