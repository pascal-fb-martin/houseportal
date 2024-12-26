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
 * houselog_sensor.c - An application log recording modules (sensor data).
 *
 * SYNOPSYS:
 *
 * void houselog_sensor_initialize (const char *application,
 *                                  int argc, const char **argv);
 *
 *    Initialize the environment required to record sensor data. This must be
 *    the first function that the application calls.
 *
 * void houselog_sensor_data (const struct timeval *timestamp,
 *                            const char *location, const char *name,
 *                            const char *value, const char *unit);
 *
 *    Submit a new sensor data record. The timestamp parameter is used as
 *    a one millisecond precision time (microseconds are ignored).
 *
 * void houselog_sensor_numeric (const struct timeval *timestamp,
 *                               const char *location, const char *name,
 *                               long long value, const char *unit);
 *
 *    Submit a new sensor data record. This is a numeric variant of
 *    houselog_sensor_data().
 *
 * void houselog_sensor_flush (void);
 *
 *    Force transmission of pending sensor data. The data is buffered until
 *    the buffer is full, or the flush is requested. This mechanism is
 *    intended for applications that receive sensor data in packets and know
 *    when it is appropriate to flush.
 *
 * void houselog_sensor_background (time_t now);
 *
 *    This function must be called a regular intervals for background
 *    processing, e.g. cleanup of expired resources, file backup, etc.
 *
 * There is no mechanism for web access to the local sensor data: web access
 * is provided by the historical service.
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
#include "houselog_sensor.h"

static const char *LogName = "portal";
static const char *PortalHost = 0;

static char LocalHost[256] = {0};

// Keep the most recent events. This is a relatively long
// history because it is used to feed the web event pages.
//
struct SensorRecord {
    struct timeval timestamp;
    int    unsaved;
    char   location[32];
    char   name[32];
    char   value[16];
    char   unit[16];
};

#define SENSOR_DEPTH 256

static struct SensorRecord SensorHistory[SENSOR_DEPTH];
static int SensorCursor = 0;
static long SensorLatestId = 0;
static long SensorLastFlushed = 0;

static time_t SensorLastFlushTime = 0;

static void safecpy (char *t, const char *s, int size) {
    if (s) {
        strncpy (t, s, size);
        t[size-1] = 0;
    } else {
        t[0] = 0;
    }
}

static int houselog_sensor_getheader (time_t now, char *buffer, int size) {

    if (PortalHost) {
        return snprintf (buffer, size,
                        "{\"host\":\"%s\",\"proxy\":\"%s\",\"apps\":[\"%s\"],"
                            "\"timestamp\":%ld,\"%s\":{\"latest\":%ld",
                        LocalHost, PortalHost, LogName,
                            (long)now, LogName, SensorLatestId);
    }
    return snprintf (buffer, size,
                    "{\"host\":\"%s\",\"apps\":[\"%s\"],"
                        "\"timestamp\":%ld,\"%s\":{\"latest\":%ld",
                    LocalHost, LogName,
                        (long)now, LogName, SensorLatestId);
}

static const char *houselog_sensor_json (time_t now) {

    static char buffer[128+SENSOR_DEPTH*(sizeof(struct SensorRecord)+24)] = {0};

    const char *prefix = "";
    int length;
    int i;

    length = houselog_sensor_getheader (now, buffer, sizeof(buffer));
    length += snprintf (buffer+length, sizeof(buffer)-length, ",\"sensor\":[");

    for (i = SensorCursor + 1; i != SensorCursor; ++i) {
        if (i >= SENSOR_DEPTH) {
            i = 0;
            if (!SensorCursor) break;
        }
        struct SensorRecord *cursor = SensorHistory + i;

        if (!(cursor->timestamp.tv_sec)) continue;
        if (!(cursor->unsaved)) continue;

        int wrote = snprintf (buffer+length, sizeof(buffer)-length,
                              "%s[%lld%03d,\"%s\",\"%s\",\"%s\",\"%s\"]",
                              prefix,
                              (long long)(cursor->timestamp.tv_sec),
                              (int)(cursor->timestamp.tv_usec/1000),
                              cursor->location,
                              cursor->name,
                              cursor->value,
                              cursor->unit);
        if (wrote >= sizeof(buffer)-length) {
            buffer[length] = 0;
            break;
        }
        length += wrote;
        prefix = ",";
        cursor->unsaved = 2;
    }
    if (prefix[0] == 0) return 0; // We did not include any data.

    snprintf (buffer+length, sizeof(buffer)-length, "]}}");
    return buffer;
}

void houselog_sensor_flush (void) {

    const char *data = houselog_sensor_json (time(0));
    if (!data) return; // Nothing to propagate.

    int newunsaved = 1;
    if (houselog_storage_flush ("sensor/data", data)) {
        SensorLastFlushed = SensorLatestId;
        SensorLastFlushTime = time(0);
        newunsaved = 0;
    }
    int i;
    for (i = SENSOR_DEPTH-1; i >= 0; --i) {
        if (SensorHistory[i].unsaved == 2) SensorHistory[i].unsaved = newunsaved;
    }
}

void houselog_sensor_data (const struct timeval *timestamp,
                           const char *location,
                           const char *name,
                           const char *value,
                           const char *unit) {

    struct SensorRecord *cursor = SensorHistory + SensorCursor;

    cursor->timestamp = *timestamp;

    safecpy (cursor->location, location, sizeof(cursor->location));
    safecpy (cursor->name, name, sizeof(cursor->name));
    safecpy (cursor->value, value, sizeof(cursor->value));
    safecpy (cursor->unit, unit, sizeof(cursor->unit));
    cursor->unsaved = 1;

    SensorCursor += 1;
    if (SensorCursor >= SENSOR_DEPTH) SensorCursor = 0;
    cursor = SensorHistory + SensorCursor;
    if ((cursor->timestamp.tv_sec) && (cursor->unsaved)) {
        houselog_sensor_flush (); // Send for storage before deleting.
    }
    cursor->timestamp.tv_sec = 0;
    cursor->unsaved = 0;

    if (SensorLatestId == 0) {
        // Seed the latest event ID based on the first event's time.
        // This makes it random enough to make its value change after
        // a restart.
        SensorLatestId = (long) (time(0) & 0xffff);
    }
    SensorLatestId += 1;
}

void houselog_sensor_numeric (const struct timeval *timestamp,
                              const char *location, const char *name,
                              long long value, const char *unit) {
    char ascii[32];
    snprintf (ascii, sizeof(ascii), "%lld", value);
    houselog_sensor_data (timestamp, location, name, ascii, unit);
}


void houselog_sensor_initialize (const char *name, int argc, const char **argv) {
    int i;
    const char *portal = 0;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match("-portal-server=", argv[i], &portal)) continue;
    }
    if (name) LogName = strdup(name);
    gethostname (LocalHost, sizeof(LocalHost));
    PortalHost = portal ? portal : LocalHost;
}

void houselog_sensor_background (time_t now) {

    houselog_storage_background (now);

    if (SensorLastFlushed != SensorLatestId) {
        // The default flush timer is slow because the application is
        // responsible for calling the flush when appropriate.
        //
        if (now < SensorLastFlushTime + 10) return;
        houselog_sensor_flush ();
        SensorLastFlushTime = now;
    }
}

