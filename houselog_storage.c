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
 * houselog_storage.c - A module for sending logs to historical services.
 *
 * SYNOPSYS:
 *
 * int houselog_storage_flush (const char *logtype, const char *data);
 *
 *    Send the log data to all known history services.
 *
 * void houselog_storage_background (time_t now);
 *
 *    This function must be called a regular intervals for background
 *    processing, e.g. cleanup of expired resources, file backup, etc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"

#include "houselog_storage.h"
#include "housediscover.h"

#define DEBUG if (echttp_isdebug()) printf

struct PendingContext {
    const char *logtype;
    char       *data;
    int         length;
    int         busy;
};

#define REQUESTSMAX 8
static struct PendingContext StoragePendingRequests[REQUESTSMAX];
static int                   StoragePendingRequestsCount = 0;

static struct PendingContext *houselog_storage_start (const char *logtype) {

    int i;
    for (i = 0; i < StoragePendingRequestsCount; ++i) {
        if (strcmp (logtype, StoragePendingRequests[i].logtype)) continue;
        if (StoragePendingRequests[i].busy > 0) return 0;
        return StoragePendingRequests + i;
    }
    // New log type.
    if (StoragePendingRequestsCount >= REQUESTSMAX) return 0; // Filled up.
    StoragePendingRequests[i].logtype = strdup (logtype);
    StoragePendingRequests[i].busy = 0;
    StoragePendingRequestsCount += 1;

    return StoragePendingRequests + i;
}

static void houselog_storage_response
                (void *context, int status, char *data, int length) {

   struct PendingContext *request = (struct PendingContext *)context;

   status = echttp_redirected("POST");
   if (!status) {
       echttp_submit (request->data, request->length,
                      houselog_storage_response, context);
       return;
   }

   if ((--request->busy) <= 0) { // Last response
       free (request->data);
       request->data = 0;
   }
}

static void houselog_storage_send
               (const char *service, void *context, const char *provider) {

    DEBUG ("Sendig data to %s\n", provider);

    struct PendingContext *request = (struct PendingContext *)context;
    char url[1024];
    snprintf (url, sizeof(url), "%s/log/%s", provider, request->logtype);

    const char *error = echttp_client ("POST", url);
    if (error) return;

    echttp_content_type_json();
    echttp_submit (request->data, request->length,
                   houselog_storage_response, context);
    request->busy += 1;
}

int houselog_storage_flush (const char *logtype, const char *data) {

    DEBUG ("Flushing: %s\n", data);

    struct PendingContext *request = houselog_storage_start (logtype);
    if (!request) return 0; // Cannot do it for now.

    request->data = strdup (data);
    request->length = strlen(request->data);

    housediscovered ("history", request, houselog_storage_send);
    if (request->busy) return 1; // Pending.

    // No request was issued: no service is available. Cancel the request.
    free (request->data);
    request->data = 0;
    return 0;
}

void houselog_storage_background (time_t now) {
    housediscover (now);
}

