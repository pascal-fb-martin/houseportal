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
 * void housecapture_record_timed (const struct timeval *timestamp,
 *                                 int category,
 *                                 const char *object,
 *                                 const char *action,
 *                                 const char *format, ...);
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
 *    If timestamp is null, the housecapture module uses the current time.
 *    Using a null timestamp is fine in most applications, except if captured
 *    events are buffered outside of this module and recorded on a periodic
 *    basis.
 *
 * void housecapture_record (int category,
 *                           const char *object,
 *                           const char *action,
 *                           const char *format, ...);
 *
 *    This is a variant of housecapture_record_timed() with a null timestamp,
 *    declared as a macro in housecapture.h but described here because this
 *    is the main API for most applications.
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
    char   object[32];
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
                              "%s[%lld%03d,\"%s\",\"%s\",\"%s\",\"%s\"]",
                              prefix,
                              (long long)(cursor->timestamp.tv_sec),
                              (int)(cursor->timestamp.tv_usec/1000),
                              cursor->category,
                              cursor->object,
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

static void housecapture_setfilter (int index, time_t now,
                                    const char *object,
                                    const char *action,
                                    const char *data) {

    struct CaptureRecord *filter = CaptureFilter + index;

    filter->timestamp.tv_sec = now;
    if (object) safecpy (filter->object, object, sizeof(filter->object));
    else filter->object[0] = 0;
    if (action) safecpy (filter->action, action, sizeof(filter->action));
    else filter->action[0] = 0;
    if (data) safecpy (filter->data, data, sizeof(filter->data));
    else filter->data[0] = 0;
}

static const char *housecapture_webstart (const char *method, const char *uri,
                                          const char *data, int length) {

    const char *category = echttp_parameter_get("cat");
    const char *object = echttp_parameter_get("obj");
    const char *action = echttp_parameter_get("act");
    const char *pattern = echttp_parameter_get("data");

    int i;
    time_t now = time(0);
    if (category) {
       for (i = CaptureFilterCount - 1; i >= 0; --i) {
          if (!strcmp (CaptureFilter[i].category, category)) break;
       }
       if (i < 0) goto failed; // Invalid category, ignore.
       housecapture_setfilter (i, now, object, action, pattern);
    } else {
       if (CaptureFilterCount <= 0) goto failed; // Edge case protection.
       for (i = CaptureFilterCount - 1; i >= 0; --i) {
          housecapture_setfilter (i, now, object, action, pattern);
       }
    }
    CaptureLastRequest = now;
    housecapture_updated ();
    return "";

failed:
    echttp_error (404, "No category");
    return "";
}

static const char *housecapture_webstop (const char *method, const char *uri,
                                         const char *data, int length) {
    housecapture_stop();
    return "";
}

static void housecapture_new (const struct timeval *timestamp,
                              const char *category,
                              const char *object,
                              const char *action,
                              const char *text) {

    struct CaptureRecord *cursor = CaptureHistory + CaptureCursor;

    if (timestamp) {
       cursor->timestamp = *timestamp;
    } else {
       gettimeofday (&(cursor->timestamp), 0);
    }

    safecpy (cursor->category, category, sizeof(cursor->category));
    safecpy (cursor->object, object, sizeof(cursor->object));
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

// Note: housecapture_record() is a macro that invokes
// housecapture_record_timed() with a null timestamp.
//
void housecapture_record_timed (const struct timeval *timestamp,
                                int category,
                                const char *object,
                                const char *action,
                                const char *format, ...) {

    if (!housecapture_active (category)) return;

    struct CaptureRecord *filter = CaptureFilter + category;

    if (filter->object[0] && (!strstr (object, filter->object))) return;
    if (filter->action[0] && (!strstr (action, filter->action))) return;

    char text[sizeof(filter->data)];
    va_list ap;
    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    if (filter->data[0] && (!strstr (text, filter->data))) return;

    housecapture_new (timestamp, filter->category, object, action, text);
}

static void housecapture_route (const char *root,
                                const char *endpoint, echttp_callback *call) {
    char uri[256];
    snprintf (uri, sizeof(uri), "%s/capture/%s", root, endpoint);
    echttp_route_uri (strdup(uri), call);
}

void housecapture_initialize (const char *root, int argc, const char **argv) {

    gethostname (LocalHost, sizeof(LocalHost));

    housecapture_route (root, "info",  housecapture_webinfo);
    housecapture_route (root, "get",   housecapture_webget);
    housecapture_route (root, "start", housecapture_webstart);
    housecapture_route (root, "stop",  housecapture_webstop);
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

