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
 * houselog_live.c - An application log recording modules (live portion).
 *
 * SYNOPSYS:
 *
 * void houselog_initialize (const char *application,
 *                           int argc, const char **argv);
 *
 *    Initialize the environment required to record logs. This must be
 *    the first function that the application calls.
 *
 * void houselog_trace (const char *file, int line, const char *level,
 *                      const char *object,
 *                      const char *format, ...);
 *
 *    Record a new trace. The first 3 parameters are handled by macros:
 *    HOUSE_INFO, HOUSE_WARNING, HOUSE_FAILURE.
 *
 * void houselog_event (const char *category,
 *                      const char *object,
 *                      const char *action,
 *                      const char *format, ...);
 *
 *    Record a new event.
 *
 * void houselog_event_local (const char *category,
 *                            const char *object,
 *                            const char *action,
 *                            const char *format, ...);
 *
 *    Record a new local event. Local events are not propagated to the
 *    history storage services: they will only appear on this service's
 *    event page.
 *    This is typically used with events that are only useful when
 *    troubleshooting that specific service.
 *
 * void houselog_background (time_t now);
 *
 *    This function must be called a regular intervals for background
 *    processing, e.g. cleanup of expired resources, file backup, etc.
 *
 * const char *houselog_host (void);
 *
 *    Return the name of the local machine, as used in the logs.
 *
 * Event are stored through the houselog_storage.c module: this portion
 * only handles the storage to RAM of the latest log entries.
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

#include "houselog.h"
#include "houselog_storage.h"
#include "housediscover.h"

static const char *LogName = "portal";
static const char *PortalHost = 0;

static char LocalHost[256] = {0};

// Keep the most recent events. This is a relatively long
// history because it is used to feed the web event pages.
//
struct EventRecord {
    struct timeval timestamp;
    int    unsaved;
    char   category[32];
    char   object[32];
    char   action[16];
    char   description[128];
};

#define EVENT_DEPTH 256

static struct EventRecord EventHistory[EVENT_DEPTH];
static int EventCursor = 0;
static long EventLatestId = 0;
static long EventLastFlushed = 0;

// Keep the most recent traces. This is a short history
// because it is only used to buffer before sending to storage.
//
struct TraceRecord {
    struct timeval timestamp;
    int    unsaved;
    char   file[32];
    int    line;
    char   level[12];
    char   object[16];
    char   description[128];
};

#define TRACE_DEPTH 16

static struct TraceRecord TraceHistory[TRACE_DEPTH];
static int TraceCursor = 0;
static long TraceLatestId = 0;
static long TraceLastFlushed = 0;

static void safecpy (char *t, const char *s, int size) {
    if (s) {
        strncpy (t, s, size);
        t[size-1] = 0;
    } else {
        t[0] = 0;
    }
}

static int houselog_getheader (time_t now, char *buffer, int size) {

    echttp_content_type_json ();

    if (PortalHost) {
        return snprintf (buffer, size,
                        "{\"host\":\"%s\",\"proxy\":\"%s\",\"apps\":[\"%s\"],"
                            "\"timestamp\":%lld,\"%s\":{\"latest\":%ld",
                        LocalHost, PortalHost, LogName,
                            (long long)now, LogName, EventLatestId);
    }
    return snprintf (buffer, size,
                    "{\"host\":\"%s\",\"apps\":[\"%s\"],"
                        "\"timestamp\":%lld,\"%s\":{\"latest\":%ld",
                    LocalHost, LogName,
                        (long long)now, LogName, EventLatestId);
}

static const char *houselog_trace_json (time_t now) {

    static char buffer[128+TRACE_DEPTH*(sizeof(struct TraceRecord)+24)] = {0};

    const char * prefix = "";
    int length;
    int i;

    length = houselog_getheader (now, buffer, sizeof(buffer));
    length += snprintf (buffer+length, sizeof(buffer)-length, ",\"traces\":[");

    for (i = TraceCursor + 1; i != TraceCursor; ++i) {
        if (i >= TRACE_DEPTH) {
            i = 0;
            if (!TraceCursor) break;
        }
        struct TraceRecord *cursor = TraceHistory + i;

        if (!(cursor->timestamp.tv_sec)) continue;
        if (!(cursor->unsaved)) continue;

        int wrote = snprintf (buffer+length, sizeof(buffer)-length,
                              "%s[%lld%03d,\"%s\",%d,\"%s\",\"%s\",\"%s\"]",
                              prefix,
                              (long long)(cursor->timestamp.tv_sec),
                              (int)(cursor->timestamp.tv_usec/1000),
                              cursor->file,
                              cursor->line,
                              cursor->level,
                              cursor->object,
                              cursor->description);
        if (wrote >= sizeof(buffer)-length) {
            buffer[length] = 0;
            break;
        }
        length += wrote;
        prefix = ",";
        cursor->unsaved = 2;
    }
    snprintf (buffer+length, sizeof(buffer)-length, "]}}");
    return buffer;
}

static const char *houselog_event_json (time_t now, int filtered) {

    static char buffer[128+EVENT_DEPTH*(sizeof(struct EventRecord)+24)] = {0};

    const char *prefix = "";
    int length;
    int i;

    length = houselog_getheader (now, buffer, sizeof(buffer));
    length += snprintf (buffer+length, sizeof(buffer)-length, ",\"events\":[");

    for (i = EventCursor + 1; i != EventCursor; ++i) {
        if (i >= EVENT_DEPTH) {
            i = 0;
            if (!EventCursor) break;
        }
        struct EventRecord *cursor = EventHistory + i;

        if (!(cursor->timestamp.tv_sec)) continue;
        if (filtered && !(cursor->unsaved)) continue;

        int wrote = snprintf (buffer+length, sizeof(buffer)-length,
                              "%s[%lld%03d,\"%s\",\"%s\",\"%s\",\"%s\"]",
                              prefix,
                              (long long)(cursor->timestamp.tv_sec),
                              (int)(cursor->timestamp.tv_usec/1000),
                              cursor->category,
                              cursor->object,
                              cursor->action,
                              cursor->description);
        if (wrote >= sizeof(buffer)-length) {
            buffer[length] = 0;
            break;
        }
        length += wrote;
        prefix = ",";
        if (filtered) cursor->unsaved = 2;
    }
    if (prefix[0] == 0) return 0; // We did not include any event.

    snprintf (buffer+length, sizeof(buffer)-length, "]}}");
    return buffer;
}

static void houselog_event_flush (void) {

    // We may not have anything to propagate if the new events were all local.
    //
    const char *data = houselog_event_json (time(0), 1);
    if (!data) return; // Nothing to propagate.

    int newunsaved = 1;
    if (houselog_storage_flush ("events", data)) {
        EventLastFlushed = EventLatestId;
        newunsaved = 0;
    }
    int i;
    for (i = EVENT_DEPTH-1; i >= 0; --i) {
        if (EventHistory[i].unsaved == 2) EventHistory[i].unsaved = newunsaved;
    }
}

static void houselog_trace_flush (void) {

    int newunsaved = 1;
    if (houselog_storage_flush ("traces", houselog_trace_json (time(0)))) {
        TraceLastFlushed = TraceLatestId;
        newunsaved = 0;
    }
    int i;
    for (i = TRACE_DEPTH-1; i >= 0; --i) {
        if (TraceHistory[i].unsaved == 2) TraceHistory[i].unsaved = newunsaved;
    }
}

static const char *houselog_weblatest (const char *method, const char *uri,
                                       const char *data, int length) {

    static char buffer[256];
    int written = houselog_getheader (time(0), buffer, sizeof(buffer));
    snprintf (buffer+written, sizeof(buffer)-written, "}}");
    return buffer;
}

static const char *houselog_webget (const char *method, const char *uri,
                                    const char *data, int length) {

    echttp_content_type_json ();
    return houselog_event_json (time(0), 0); // Show the most recent data.
}

void houselog_trace (const char *file, int line, const char *level,
                     const char *object,
                     const char *format, ...) {

    va_list ap;
    struct TraceRecord *cursor = TraceHistory + TraceCursor;

    gettimeofday (&(cursor->timestamp), 0);

    va_start (ap, format);
    vsnprintf (cursor->description, sizeof(cursor->description), format, ap);
    va_end (ap);

    safecpy (cursor->file, file, sizeof(cursor->file));
    cursor->line = line;
    safecpy (cursor->level, level, sizeof(cursor->level));
    safecpy (cursor->object, object, sizeof(cursor->object));
    cursor->unsaved = 1;

    TraceCursor += 1;
    if (TraceCursor >= TRACE_DEPTH) TraceCursor = 0;
    cursor = TraceHistory + TraceCursor;
    if ((cursor->timestamp.tv_sec) && (cursor->unsaved)) {
        houselog_trace_flush (); // Send for storage before deleting.
    }
    cursor->timestamp.tv_sec = 0;
    cursor->unsaved = 0;

    if (TraceLatestId == 0) {
        // Seed the latest event ID based on the first event's time.
        // This makes it random enough to make its value change after
        // a restart.
        TraceLatestId = (long) (time(0) & 0xffff);
    }
    TraceLatestId += 1;

    if (echttp_isdebug())
        printf ("%s %s, %d: %s %s\n", level, file, line, object, cursor->description);
}

static void houselog_event_new (const char *category,
                                const char *object,
                                const char *action,
                                const char *text, int propagate) {

    struct EventRecord *cursor = EventHistory + EventCursor;

    gettimeofday (&(cursor->timestamp), 0);

    safecpy (cursor->category, category, sizeof(cursor->category));
    safecpy (cursor->object, object, sizeof(cursor->object));
    safecpy (cursor->action, action, sizeof(cursor->action));
    safecpy (cursor->description, text, sizeof(cursor->description));
    cursor->unsaved = propagate;

    EventCursor += 1;
    if (EventCursor >= EVENT_DEPTH) EventCursor = 0;
    cursor = EventHistory + EventCursor;
    if ((cursor->timestamp.tv_sec) && (cursor->unsaved)) {
        houselog_event_flush (); // Send for storage before deleting.
    }
    cursor->timestamp.tv_sec = 0;
    cursor->unsaved = 0;

    if (EventLatestId == 0) {
        // Seed the latest event ID based on the first event's time.
        // This makes it random enough to make its value change after
        // a restart.
        EventLatestId = (long) (time(0) & 0xffff);
    }
    EventLatestId += 1;
}

void houselog_event (const char *category,
                     const char *object,
                     const char *action,
                     const char *format, ...) {

    char text[sizeof(EventHistory[0].description)];

    va_list ap;
    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    houselog_event_new (category, object, action, text, 1); // Propagated
}

void houselog_event_local (const char *category,
                           const char *object,
                           const char *action,
                           const char *format, ...) {

    char text[sizeof(EventHistory[0].description)];

    va_list ap;
    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    houselog_event_new (category, object, action, text, 0); // Not propagated
}

void houselog_initialize (const char *name, int argc, const char **argv) {
    int i;
    char uri[256];
    const char *portal = 0;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-portal-server=", argv[i], &portal)) continue;
    }
    if (name) LogName = strdup(name);
    gethostname (LocalHost, sizeof(LocalHost));
    PortalHost = portal ? portal : LocalHost;

    snprintf (uri, sizeof(uri), "/%s/log/events", LogName);
    echttp_route_uri (strdup(uri), houselog_webget);

    snprintf (uri, sizeof(uri), "/%s/log/latest", LogName);
    echttp_route_uri (strdup(uri), houselog_weblatest);

    // Alternate paths for application-independent web pages.
    // (The log files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/log/events", houselog_webget);
    echttp_route_uri ("/log/latest", houselog_weblatest);

    houselog_background (time(0)); // Initial state (with nothing to flush).

    // Mark the point of (re)start in both logs.
    houselog_trace (HOUSE_INFO, LogName, "STARTING", "");
    houselog_event ("SERVICE", LogName, "STARTING", "");
}

void houselog_background (time_t now) {

    static time_t EventLastFlushTime = 0;

    houselog_storage_background (now);

    if (EventLastFlushed != EventLatestId) {
        if (now < EventLastFlushTime + 2) return;
        houselog_event_flush ();
        EventLastFlushTime = now;
    }

    // We buffer the traces less because these are for debug.
    if (TraceLastFlushed != TraceLatestId) {
        houselog_trace_flush ();
    }
}

const char *houselog_host (void) {
    return LocalHost;
}

