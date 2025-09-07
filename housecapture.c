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
 * housecapture.c - An IO capture modules
 *
 * SYNOPSYS:
 *
 * void housecapture_initialize (const char *root,
 *                               int argc, const char **argv);
 *
 *    Initialize the environment required to record captured data.
 *    This must be the first function that the application calls.
 *
 * int housecapture_register (const char *category);
 *
 *    Register a capture category. A category typically represents a type
 *    of data captured (e.g. EVENT, SENSOR, NMEA, etc.) This function should
 *    be called once for each category, during the application initialization.
 *    This returns an index for the category, which must be used for any new
 *    record, or when checking if any capture is active.
 *
 * int housecapture_registered (void);
 *
 *    Return the number of categories that have been registered so far.
 *    This can be used to walk through all categories, typically to
 *    propagate the capture filters to another thread.
 *
 * time_t housecapture_active (int category);
 *
 *    Return the current capture timer if capture is active for the
 *    specified category, 0 otherwise.
 *
 *    Note that housecapture_record() performs the same checks, however
 *    the application might have to execute some expensive processing
 *    to populate all parameters for housecapture_record(): this function
 *    can be used to avoid the overhead.
 *
 *    A caller will typically use the value returned as a boolean, but
 *    the actual timer value might be used to propagate the capture state
 *    to another thread.
 *
 * void housecapture_record (int category,
 *                           const char *action,
 *                           const char *format, ...);
 *
 *    Record new capture data. All capture data is local: there is no
 *    intent to consolidate and archive capture data. Use events for
 *    significant information worth archiving.
 *
 *    Nothing is recorded if the capture is not active for the specified
 *    category, or if there is a filter condition that the data does not meet.
 *
 *    An action represents provides some context for the data, for example
 *    "RECEIVE", "SEND", "TRIGGER", "IGNORE", etc. An action will typically
 *    not appear in the data itself. The action also provides an additional
 *    filter criteria.
 *
 * void housecapture_background (time_t now);
 *
 *    This function must be called a regular intervals for background
 *    processing, e.g. cleanup of expired resources, file backup, etc.
 *
 * Capture records are stored locally. There is no polling optimization,
 * like check for changes, because capture typically change multiple times
 * per second.
 */

#include <unistd.h>
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "echttp.h"
#include "echttp_static.h"

#include "housecapture.h"

static char LocalHost[256] = {0};

// Keep the most recent capture records. This is a relatively long
// history because this is used to feed the web capture pages.
//
struct CaptureRecord {
    struct timeval timestamp;
    char   category[16];
    char   action[16];
    char   data[128];
};

#define CAPTURE_DEPTH 256
static struct CaptureRecord CaptureHistory[CAPTURE_DEPTH];
static int CaptureCursor = 0;

#define CAPTURE_FILTER 16
static struct CaptureRecord CaptureFilter[CAPTURE_FILTER];
static int CaptureFilterCount = 0;

#define CAPTURE_DEADLINE 5
static time_t CaptureLastRequest = 0;

static long CaptureLatestId = 0;


static void safecpy (char *t, const char *s, int size) {
    if (s) {
        strncpy (t, s, size);
        t[size-1] = 0;
    } else {
        t[0] = 0;
    }
}

static void housecapture_updated (void) {

    if (CaptureLatestId == 0) {
       // Seed the latest capture ID based on the currrent time,
       // to make it random enough.
       CaptureLatestId = (long) (time(0) & 0xffff);
    }
    CaptureLatestId += 1;
}

static void housecapture_stop (void) {

    if (CaptureLastRequest > 0) {
       int i;
       for (i = CaptureFilterCount - 1; i >= 0; --i) {
          if (CaptureFilter[i].timestamp.tv_sec) {
             CaptureFilter[i].timestamp.tv_sec = 0;
             CaptureFilter[i].action[0] = 0;
             CaptureFilter[i].data[0] = 0;
          }
       }
       for (i = CAPTURE_DEPTH - 1; i >= 0; --i) {
          CaptureHistory[i].timestamp.tv_sec = 0;
       }
       CaptureLastRequest = 0;
    }
}

static int housecapture_head (time_t now, char *buffer, int size) {

    return snprintf (buffer, size,
                     "{\"host\":\"%s\",\"timestamp\":%lld,\"latest\":%ld,\"capture\":[",
                     LocalHost, (long long)now, CaptureLatestId);
}

static const char *housecapture_json (time_t now) {

    static char buffer[128+CAPTURE_DEPTH*(sizeof(struct CaptureRecord)+24)] = {0};

    const char *prefix = "";
    int length;
    int i;

    length = housecapture_head (now, buffer, sizeof(buffer));

    for (i = CaptureCursor + 1; i != CaptureCursor; ++i) {
        if (i >= CAPTURE_DEPTH) {
            i = 0;
            if (!CaptureCursor) break;
        }
        struct CaptureRecord *cursor = CaptureHistory + i;

        if (!(cursor->timestamp.tv_sec)) continue;

        int wrote = snprintf (buffer+length, sizeof(buffer)-length,
                              "%s[%lld%03d,\"%s\",\"%s\",\"%s\"]",
                              prefix,
                              (long long)(cursor->timestamp.tv_sec),
                              (int)(cursor->timestamp.tv_usec/1000),
                              cursor->category,
                              cursor->action,
                              cursor->data);
        if (wrote >= sizeof(buffer)-length) {
            buffer[length] = 0;
            break;
        }
        length += wrote;
        prefix = ",";
    }
    snprintf (buffer+length, sizeof(buffer)-length, "]}");
    return buffer;
}

static const char *housecapture_webget (const char *method, const char *uri,
                                        const char *data, int length) {

    time_t now = time(0);
    echttp_content_type_json ();

    if (!CaptureLastRequest) {
        echttp_error (409, "No active capture");
        return "";
    }
    CaptureLastRequest = now;

    // This is a way to check if there is something new without having
    // to submit a second request if there is something. Derived from
    // the If-Modified-Since HTTP header item.
    //
    const char *known = echttp_parameter_get("known");
    if (known && (CaptureLatestId == atol (known))) {
       echttp_error (304, "Not Modified");
       return "";
    }
    return housecapture_json (now); // Show the most recent data.
}

static const char *housecapture_webinfo (const char *method, const char *uri,
                                         const char *data, int length) {

    static char buffer[128+CAPTURE_FILTER*(sizeof(CaptureFilter[0].category)+4)] = {0};

    echttp_content_type_json ();
    time_t now = time(0);
    int cursor = housecapture_head (now, buffer, sizeof(buffer));

    const char *prefix = "";
    int i;
    for (i = CaptureFilterCount - 1; i >= 0; --i) {
       cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                           "%s\"%s\"", prefix, CaptureFilter[i].category);
       if (cursor >= sizeof(buffer)) return "";
       prefix = ",";
    }
    snprintf (buffer+cursor, sizeof(buffer)-cursor, "]}");
    return buffer;
}

static const char *housecapture_webstart (const char *method, const char *uri,
                                          const char *data, int length) {

    const char *category = echttp_parameter_get("cat");
    const char *action = echttp_parameter_get("act");
    const char *pattern = echttp_parameter_get("data");

    time_t now = time(0);
    if (category) {
       int i;
       for (i = CaptureFilterCount - 1; i >= 0; --i) {
          if (!strcmp (CaptureFilter[i].category, category)) {
              CaptureFilter[i].timestamp.tv_sec = now;
              break;
          }
       }
       if (i < 0) return ""; // Invalid, ignore.

       if (action) safecpy (CaptureFilter[i].action, action, sizeof(CaptureFilter[0].action));
       else CaptureFilter[i].action[0] = 0;
       if (pattern) safecpy (CaptureFilter[i].data, pattern, sizeof(CaptureFilter[0].data));
       else CaptureFilter[i].data[0] = 0;
    } else {
       int i;
       for (i = CaptureFilterCount - 1; i >= 0; --i) {
          CaptureFilter[i].timestamp.tv_sec = now;
          if (action) safecpy (CaptureFilter[i].action, action, sizeof(CaptureFilter[0].action));
          else CaptureFilter[i].action[0] = 0;
          if (pattern) safecpy (CaptureFilter[i].data, pattern, sizeof(CaptureFilter[0].data));
          else CaptureFilter[i].data[0] = 0;
       }
    }
    CaptureLastRequest = now;
    housecapture_updated ();
    return "";
}

static const char *housecapture_webstop (const char *method, const char *uri,
                                         const char *data, int length) {
    housecapture_stop();
    return "";
}

static void housecapture_new (const char *category,
                              const char *action,
                              const char *text) {

    struct CaptureRecord *cursor = CaptureHistory + CaptureCursor;

    gettimeofday (&(cursor->timestamp), 0);

    safecpy (cursor->category, category, sizeof(cursor->category));
    safecpy (cursor->action, action, sizeof(cursor->action));
    safecpy (cursor->data, text, sizeof(cursor->data));

    CaptureCursor += 1;
    if (CaptureCursor >= CAPTURE_DEPTH) CaptureCursor = 0;
    cursor = CaptureHistory + CaptureCursor;
    cursor->timestamp.tv_sec = 0;

    housecapture_updated ();
}

int housecapture_register (const char *category) {

    int i;
    for (i = CaptureFilterCount - 1; i >= 0; --i) {
       if (!strcmp (CaptureFilter[i].category, category)) return i;
    }
    // Unknown category, register it now.
    if (CaptureFilterCount < CAPTURE_FILTER) {
       safecpy (CaptureFilter[CaptureFilterCount].category,
                 category,
                 sizeof(CaptureFilter[0].category));
       return CaptureFilterCount++;
    }
    return -1;
}

int housecapture_registered (void) {
    return CaptureFilterCount;
}

time_t housecapture_active (int index) {

    if (!CaptureLastRequest) return 0; // Nobody is listening.

    if (index < 0 || index >= CAPTURE_FILTER) return 0;
    if (CaptureFilter[index].timestamp.tv_sec) return CaptureLastRequest;
    return 0;
}

void housecapture_record (int category,
                          const char *action,
                          const char *format, ...) {

    if (!housecapture_active (category)) return;

    if (CaptureFilter[category].action[0] &&
        (!strstr (action, CaptureFilter[category].action))) return;

    char text[sizeof(CaptureHistory[0].data)];
    va_list ap;
    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    if (CaptureFilter[category].data[0] &&
        (!strstr (text, CaptureFilter[category].data))) return;

    housecapture_new (CaptureFilter[category].category, action, text);
}

void housecapture_initialize (const char *root, int argc, const char **argv) {

    char uri[256];

    gethostname (LocalHost, sizeof(LocalHost));

    snprintf (uri, sizeof(uri), "%s/capture/info", root);
    echttp_route_uri (strdup(uri), housecapture_webinfo);

    snprintf (uri, sizeof(uri), "%s/capture/get", root);
    echttp_route_uri (strdup(uri), housecapture_webget);

    snprintf (uri, sizeof(uri), "%s/capture/start", root);
    echttp_route_uri (strdup(uri), housecapture_webstart);

    snprintf (uri, sizeof(uri), "%s/capture/stop", root);
    echttp_route_uri (strdup(uri), housecapture_webstop);
}

void housecapture_background (time_t now) {

    // Stop capture after idle deadline: no client is active anymore.
    //
    if (CaptureLastRequest > 0) {
       if (CaptureLastRequest + CAPTURE_DEADLINE < now) {
          housecapture_stop ();
       }
    }
}

