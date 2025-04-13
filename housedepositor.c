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
 * void housedepositor_default (const char *arg);
 *
 *    Set default values for command line options. Call the function once for
 *    each options to set a default for. This function must be called before
 *    housedepositor_initialize().
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
 * void housedepositor_put (const char *repository,
 *                          const char *name,
 *                          const char *data, int size);
 *
 *    Update the named file in all discovered HouseDepot repositories.
 *    (The repository and name are typically constants, while the group is
 *    provided as a command line parameter.)
 *
 * void housedepositor_put_file (const char *repository,
 *                               const char *name,
 *                               const char *filename);
 *
 *    This is the same as housedepositor_put(), but the data is to be read
 *    from the specified file. The HouseDepot data's timestamp is set to the
 *    timestamp of the file as well. One benefit is that there is no local
 *    limit to the size of the data (there could be a limit on the server
 *    side).
 *
 * void housedepositor_periodic (time_t now);
 *
 *    Background updates.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"
#include "housedepositor.h"

#define DEBUG if (echttp_isdebug()) printf

#define DEPOT_URI_PREFIX "/depot/"

static const char *DepotGroup = "home";

static int DepotCheckPending = 0;
static int DepotScanPending = 0;

typedef struct {
    char *host;
    long long detected;
    long long scanned;
} DepotServiceEntry;

static DepotServiceEntry *DepotServices = 0;
static int                DepotServicesSize = 0;
static int                DepotServicesCount = 0;

typedef struct {
    char *uri;
    housedepositor_listener *listener;
    time_t refreshing;
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


void housedepositor_default (const char *arg) {
    if (echttp_option_match("-group=", arg, &DepotGroup)) return;
    // FUTURE: handle other future options.
    return;
}

void housedepositor_initialize (int argc, const char **argv) {

    int i;
    for (i = 1; i < argc; i++) {
        housedepositor_default (argv[i]);
    }
}

static int housedepositor_search (const char *name) {
    int i;
    for (i = 0; i < DepotCacheCount; i++) {
        if (!strcmp(DepotCache[i].uri, name)) return i;
    }
    return -1;
}

static void housedepositor_uri (char *uri, int size,
                                const char *repository, const char *name) {
    snprintf (uri, size,
              DEPOT_URI_PREFIX "%s/%s/%s", repository, DepotGroup, name);
}

static const char *housedepositor_extract_path (const char *uri) {
    static int LengthOfPrefix = 0;
    if (!LengthOfPrefix) LengthOfPrefix = strlen(DEPOT_URI_PREFIX);
    return uri + LengthOfPrefix;
}

void housedepositor_subscribe (const char *repository,
                               const char *name,
                               housedepositor_listener *listener) {
    
    if (DepotCacheCount >= MAX_CACHE) {
        houselog_trace (HOUSE_FAILURE, name,
                        "Registration cache full (file %s)", name);
        return;
    }
    
    char uri[1024];
    housedepositor_uri (uri, sizeof(uri), repository, name);
    DEBUG ("subscribe to %s\n", uri);

    int i = housedepositor_search(uri);
    if (i >= 0) {
        if (listener != DepotCache[i].listener) {
           houselog_trace (HOUSE_FAILURE, name,
                           "Registration conflict (repository %s)", repository);
        }
        return;
    }
    i = DepotCacheCount++;
    DepotCache[i].uri = strdup(uri);
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
    char *data;     // For data in memory upload
    int fd;         // For file upload.
    char *filename; // For file upload (redirected).
    int length;
    time_t timestamp;
} HouseDepositorPutContext;

static void housedepositor_put_free (HouseDepositorPutContext *request) {
    if (request->data) free (request->data);
    if (request->path) free (request->path);
    if (request->filename) free (request->filename);
    free (request);
}

static void housedepositor_put_release (HouseDepositorPutContext *request) {
    if ((--request->pending) <= 0) { // Last response.
        housedepositor_put_free (request);
    }
}

static void housedepositor_put_response
               (void *context, int status, char *data, int length) {

    HouseDepositorPutContext *request =
        (HouseDepositorPutContext *)context;

   status = echttp_redirected("PUT");
   if (!status){
       if (request->data) {
           echttp_submit (request->data, request->length,
                          housedepositor_put_response, context);
       } else {
           // Since the current request has been closed, request->fd was
           // closed as well. Must reopen.
           request->fd = open (request->filename, O_RDONLY);
           echttp_transfer (request->fd, request->length);
           echttp_submit (0, 0, housedepositor_put_response, context);
       }
       return;
   }
   
   DEBUG ("response to put of %s: %s\n", request->path, (length > 0)?data:"");

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, request->path, "HTTP code %d", status);
   }

   housedepositor_put_release (request);
}

static void housedepositor_put_iterator
               (const char *service, void *context, const char *provider) {

    HouseDepositorPutContext *request =
        (HouseDepositorPutContext *)context;
    
    char url[1024];
    
    snprintf (url, sizeof(url), "%s/%s?time=%lld",
              provider, request->path, (long long)(request->timestamp));
    const char *error = echttp_client ("PUT", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, service,
                        "cannot create socket for %s, %s", url, error);
        return;
    }
    DEBUG ("PUT %s : %s\n", url, request->data?request->data:"(file)");
    request->pending += 1;
    if (request->data) {
        echttp_submit (request->data, request->length,
                       housedepositor_put_response, context);
    } else {
        echttp_transfer (request->fd, request->length);
        echttp_submit (0, 0, housedepositor_put_response, context);
    }
}

static void housedepositor_put_submit (const char *repository,
                                       const char *name,
                                       HouseDepositorPutContext *request) {

    char uri[1024];
    time_t now = request->timestamp;

    housedepositor_uri (uri, sizeof(uri),repository, name);

    // The request path is not the full URI because the "/depot/" prefix
    // is already part of the service's address returned by the portal.
    //
    request->path = strdup(housedepositor_extract_path(uri));
    request->pending = 0;

    housediscovered ("depot", request, housedepositor_put_iterator);

    // There might have been no depot service running at this time.
    // In that case, nothing has happened: just get out.
    if (request->pending <= 0) {
        DEBUG ("No depot service detected during put.\n");
        housedepositor_put_free (request);
        return;
    }

    // Update the cache to reflect the revision that was just checked in.
    // This is to avoid reloading the same configuration data.
    //
    int cached = housedepositor_search(uri);
    if (cached >= 0) {
        DepotCache[cached].detected = now;
        DepotCache[cached].active = now;
    }
}

void housedepositor_put (const char *repository,
                         const char *name,
                         const char *data, int size) {
    
    HouseDepositorPutContext *request =
        (HouseDepositorPutContext *) malloc (sizeof(HouseDepositorPutContext));
    
    request->timestamp = time(0);

    /* We must keep a copy here because we do not know the lifespan of the
     * caller's data. By making a copy, we control that copy's lifespan
     * until all DEPOT requests have completed (or failed).
     */
    request->fd = 0;
    request->filename = 0;
    request->data = malloc (size);
    request->length = size;
    memcpy (request->data, data, size);

    housedepositor_put_submit (repository, name, request);
}

void housedepositor_put_file (const char *repository,
                              const char *name,
                              const char *filename) {

    HouseDepositorPutContext *request =
        (HouseDepositorPutContext *) malloc (sizeof(HouseDepositorPutContext));

    request->fd = open (filename, O_RDONLY);
    if (request->fd < 0) goto abort;

    struct stat fileinfo;
    if (fstat(request->fd, &fileinfo) < 0) goto abort;
    if ((fileinfo.st_mode & S_IFMT) != S_IFREG) goto abort;

    request->data = 0;
    request->filename = strdup(filename);
    request->length = fileinfo.st_size;
    request->timestamp = fileinfo.st_mtim.tv_sec;

    housedepositor_put_submit (repository, name, request);
    return;

abort:
    free (request);
    return;
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
        cache->detected = cache->active; // Don't get stuck on this file.
        return;
    }
    
    DEBUG ("response to get %s: %s\n", cache->uri, data);

    if (cache->listener) {
        cache->listener(cache->uri, cache->detected, data, length);
        cache->active = cache->detected;
    }
}

static void housedepositor_get (DepotCacheEntry *cache) {
    
    char url[1024];
    snprintf (url, sizeof(url), "http://%s%s", cache->host, cache->uri);
              
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, cache->uri,
                        "cannot create socket for %s: %s", url, error);
        return;
    }
    DEBUG ("GET %s\n", url);
    echttp_submit (0, 0, housedepositor_get_response, (void *)cache);
}

static int housedepositor_refresh (void) {
    
    int i;
    int refreshing = 0;
    time_t now = time(0);

    for (i = 0; i < DepotCacheCount; i++) {
        DepotCacheEntry *cache = DepotCache + i;
        if (!cache->detected) continue;
        if (cache->refreshing) {
            if (now > cache->refreshing + 10) {
                // Timed out. Backtrack and give up on this update.
                DEBUG ("Timed out while refreshing %s\n", cache->uri);
                houselog_trace (HOUSE_FAILURE, cache->uri, "timeout");
                cache->detected = cache->active;
                cache->refreshing = 0;
            }
            continue;
        }
        if (cache->detected != cache->active) {
            DEBUG ("Need to refresh %s (%d != %d)\n",
                   cache->uri,
                   (int)(cache->detected), (int)(cache->active));
            housedepositor_get(cache);
            cache->refreshing = now;
            refreshing += 1;
        }
    }
    return refreshing;
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
    if (DepotScanPending <= 0) {
        DEBUG ("Scan of HouseDepot services completed\n");
        DepotScanPending = 0; // Better safe than sorry.
    }
    
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
                DEBUG ("Most recent version found so far\n");
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

    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, repository,
                        "cannot create socket for %s, %s", url, error);
        return;
    }
    DEBUG ("GET %s\n", url);
    echttp_submit (0, 0, housedepositor_scan_response, context);
    DepotScanPending += 1;
}

static void housedepositor_check_response
               (void *context, int status, char *data, int length) {

    status = echttp_redirected("GET");
    if (!status){
        echttp_submit (0, 0, housedepositor_check_response, context);
        return;
    }

    DepotCheckPending -= 1;
    if (DepotCheckPending <= 0) {
        DEBUG ("Check of HouseDepot services completed\n");
        DepotCheckPending = 0; // Better safe than sorry.
    }

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, "check", "HTTP code %d", status);
        return;
    }
    DEBUG ("response to check: %s\n", data);

    ParserToken tokens[16];
    int count = 16;
   
    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "check", "JSON syntax error: %s", error);
        return;
    }
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, "check", "no data");
        return;
    }
    
    int host = echttp_json_search (tokens, ".host");
    if (host <= 0) {
        houselog_trace (HOUSE_FAILURE, "check", "no host");
        return;
    }
    const char *hostname = tokens[host].value.string;

    int updated = echttp_json_search (tokens, ".updated");
    if (updated <= 0) {
        houselog_trace (HOUSE_FAILURE, "check", "no timestamp");
        return;
    }

    int i;
    for (i = 0; i < DepotServicesCount; i++) {
        if (strcmp (hostname, DepotServices[i].host)) continue;
        DepotServices[i].detected = tokens[updated].value.integer;
        break;
    }
    if (i >= DepotServicesCount) {
        if (DepotServicesCount >= DepotServicesSize) {
            DepotServicesSize = DepotServicesCount + 16;
            DepotServices = realloc (DepotServices,
                                     DepotServicesSize*sizeof(DepotServiceEntry));
        }
        i = DepotServicesCount++;
        DepotServices[i].host = strdup (hostname);
        DepotServices[i].detected = tokens[updated].value.integer;
        DepotServices[i].scanned = 0;
    }
}

static void housedepositor_check_iterator
                (const char *service, void *context, const char *provider) {

    char url[1024];
    snprintf (url, sizeof(url), "%s/check", provider);

    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, "check",
                        "cannot create socket for %s, %s", url, error);
        return;
    }
    DEBUG ("GET %s \n", url);
    echttp_submit (0, 0, housedepositor_check_response, context);
    DepotCheckPending += 1;
}


void housedepositor_periodic (time_t now) {

    static time_t DepotCheckStart = 0;
    static time_t DepotScanStart = 0;
    
    // Make sure to handle non responsive depot service: timeout on
    // check and scan cycles. Do only one ting at a time: either check,
    // scan or refresh, but not all at the same time.
    //
    if (DepotScanPending > 0) {
        if (now <= DepotScanStart + 10) return; // wait until end of scan.
        DEBUG ("Scan timed out, refresh forced\n");
        houselog_trace (HOUSE_FAILURE, "Depot scan", "timeout");
        DepotScanPending = 0;
    }
    if (DepotCheckPending > 0) {
        if (now <= DepotCheckStart + 10) return; // wait until end of check.
        DEBUG ("Check timed out\n");
        DepotCheckPending = 0;
    }

    if (housedepositor_refresh()) return; // Busy downloading new files.

    // Do we need to scan the depot services?
    // TBD: only scan those services that need update.
    //
    int i;
    int needscan = 0;
    for (i = 0; i < DepotServicesCount; i++) {
        if (DepotServices[i].scanned != DepotServices[i].detected) {
            DepotServices[i].scanned = DepotServices[i].detected;
            needscan = 1;
        }
    }
    if (needscan) {
        DEBUG ("Scanning all depot services\n");
        for (i = 0; i < DepotCacheCount; i++) {
            DepotCache[i].detected = 0;
        }
        DepotScanPending = 0;

        for (i = 0; i < MAX_SOURCE; i++) {
            if (!DepotRepositories[i]) break;
            housediscovered
                ("depot", (void *)(DepotRepositories[i]), housedepositor_scan_iterator);
        }
        DepotScanStart = now;
        if (DepotScanPending > 0) return; // Busy now, wait.
    }

    if (now < DepotCheckStart + 5) return; // Don't check too often.

    DEBUG ("Checking all depot services\n");
    DepotCheckPending = 0;
    housediscovered ("depot", 0, housedepositor_check_iterator);
    if (DepotCheckPending)
        DepotCheckStart = now;
    else
        DEBUG ("No depot services detected\n");
}

