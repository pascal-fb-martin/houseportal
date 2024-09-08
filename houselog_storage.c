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

struct PendingRequest {
    const char *logtype;
    char       *data;
    int         length;
    int         count;
};

static void houselog_storage_response
                (void *context, int status, char *data, int length) {

   struct PendingRequest *request = (struct PendingRequest *)context;

   status = echttp_redirected("POST");
   if (!status) {
       echttp_submit (request->data, request->length,
                      houselog_storage_response, context);
       return;
   }

   if ((--request->count) <= 0) { // Last response
       free (request->data);
       free (request);
   }
}

static void houselog_storage_send
               (const char *service, void *context, const char *provider) {

    DEBUG ("Sendig data to %s\n", provider);

    struct PendingRequest *request = (struct PendingRequest *)context;
    char url[1024];
    snprintf (url, sizeof(url), "%s/log/%s", provider, request->logtype);

    const char *error = echttp_client ("POST", url);
    if (error) return;

    echttp_content_type_json();
    echttp_submit (request->data, request->length,
                   houselog_storage_response, context);
    request->count += 1;
}

int houselog_storage_flush (const char *logtype, const char *data) {

    DEBUG ("Flushing: %s\n", data);

    struct PendingRequest *request = malloc (sizeof(struct PendingRequest));

    request->logtype = logtype;
    request->data = strdup (data);
    request->length = strlen(request->data);
    request->count = 0;

    housediscovered ("history", request, houselog_storage_send);
    if (request->count) return 1; // Pending.

    // No request was issued: no service is available. Cancel the request.
    free (request->data);
    free (request);
    return 0;
}

void houselog_storage_background (time_t now) {
    housediscover (now);
}

