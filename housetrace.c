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
 * housetrace.c - A generic application trace recording modules.
 *
 * SYNOPSYS:
 *
 * void housetrace_initialize (const char *application,
 *                             int argc, const char **argv);
 *
 *    Initialize the environment required to record traces.
 *
 * void housetrace_record (const char *file, int line, const char *level,
 *                         const char *object,
 *                         const char *format, ...);
 *
 *    Provide a new trace to be recorded. The first 3 parameters are
 *    handled by macros: HOUSE_INFO, HOUSE_WARNING, HOUSE_FAILURE.
 *
 * void housetrace_periodic (time_t now);
 *
 *    This function must be called a regular intervals for background
 *    cleanup.
 *
 * This module is intended for recording application traces that are intended
 * for troubleshooting the software.
 *
 * This is not meant for recording application events meaningful to end users.
 */

#include <unistd.h>
#include <dirent.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include "echttp.h"
#include "echttp_static.h"
#include "housetrace.h"

static const char *TraceFolder = "/var/lib/house/traces";
static const char *TraceName = "portal";

static char LocalHost[256] = {0};

FILE *TraceFile = 0;


static FILE *housetrace_open (const struct tm *local, int write) {

    static struct tm Last = {0};

    char path[256];
    char buffer[512];

    snprintf (path, sizeof(path), "%s/%04d/%02d/%02d",
              TraceFolder,
              local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);

    if (write) {
        snprintf (buffer, sizeof(buffer), "mkdir -p %s", path);
        system (buffer);
    }

    snprintf (buffer, sizeof(buffer), "%s/%s.csv", path, TraceName);

    FILE *fd = fopen (buffer, write?"a":"r");
    if (fd == 0) {
        fprintf (stderr, "%s: cannot open, %s\n", strerror (errno));
        return 0;
    }

    if (write && ftell(fd) <= 0) {
        fprintf (fd, "TIMESTAMP,LEVEL,FILE,LINE,OBJECT,DESCRIPTION\n");
    }
    return fd;
}


void housetrace_record (const char *file, int line, const char *level,
                        const char *object,
                        const char *format, ...) {

    static struct tm Last = {0};

    va_list ap;
    time_t timestamp = time(0);
    struct tm local = *localtime (&timestamp);
    char description[1024];

    va_start (ap, format);
    vsnprintf (description, sizeof(description), format, ap);
    va_end (ap);

    if (local.tm_year != Last.tm_year ||
        local.tm_mon != Last.tm_mon ||
        local.tm_mday != Last.tm_mday) {

        if (TraceFile) {
            fclose (TraceFile);
            TraceFile = 0;
        }
        Last = local;
    }

    if (!TraceFile)
        TraceFile = housetrace_open (&local, 1);

    if (echttp_isdebug())
        printf ("%s %s, %d: %s %s\n",
                level, file, line, object, description);

    if (TraceFile)
        fprintf (TraceFile, "%ld,\"%s\",\"%s\",%d,\"%s\",\"%s\"\n",
                 (long) timestamp, level, file, line, object, description);
}

static const char *housetrace_history (struct tm *local) {

    static char buffer[65537];

    time_t start = mktime(local);
    int length;
    char *eol;
    const char *prefix = "";

    snprintf (buffer, sizeof(buffer),
              "{\"%s\":{\"timestamp\":%d,\"host\":\"%s\",\"traces\":[",
              TraceName, time(0), LocalHost);
    length = strlen(buffer);

    FILE *fd = housetrace_open (local, 0);

    if (fd) {
        char line[1024];
        fgets (line, sizeof(line), fd); // Consume the header.
        while (!feof(fd)) {
            line[0] = 0;
            fgets (line, sizeof(line), fd);
            if (line[0] == 0) continue;
            time_t ts = atol(line);
            if (ts < start) continue;
            eol = strchr(line, '\n');
            if (eol) *eol = 0;

            int wrote = snprintf (buffer+length, sizeof(buffer)-length,
                                  "%s[%s]", prefix, line);
            if (wrote >= sizeof(buffer) - length) {
                buffer[length] = 0;
                break;
            }
            length += strlen(buffer+length);
            prefix = ",";
        }
        fclose(fd);
    }

    snprintf (buffer+length, sizeof(buffer)-length, "]}}");
    return buffer;
}

static const char *housetrace_webhistory (const char *method, const char *uri,
                                          const char *data, int length) {

    // The default is to show the most recent traces.
    // To force a specific start time, use the "time" parameter.
    // For getting traces for another day, set the "date" parameter.
    // Both are local time relative to the server.
    //
    time_t now = time(0);

    const char *date = echttp_parameter_get ("date"); // YYYY-MM-DD
    const char *hhmm = echttp_parameter_get ("time"); // HH:MM

    time_t start = now - 600; // Default: show the last 10 minutes.
    struct tm local = *localtime (&start);

    if (date) {
        int day;
        int month;
        int year = atoi(date);
        if (year >= 2000) {
            const char *cursor = strchr (date, '-');
            if (cursor) {
                if (cursor[1] == '0') ++cursor;
                month = atoi(++cursor);
                if (month >= 1 && month <= 12) {
                    month -= 1;
                    cursor = strchr (cursor, '-');
                    if (cursor) {
                        if (cursor[1] == '0') ++cursor;
                        day = atoi(++cursor);
                        if (day >= 1 && day <= 31) {
                            local.tm_year = year - 1900;
                            local.tm_mon = month - 1;
                            local.tm_mday = day;
                        }
                    }
                }
            }
        }
    }

    if (hhmm) {
        int minute;
        int hour = atoi(hhmm);
        if (hour >= 0 && hour <= 23) {
            const char *cursor = strchr (hhmm, ':');
            if (cursor) {
                if (cursor[1] == '0') ++cursor;
                minute = atoi(++cursor);
                if (minute >= 0 && minute <= 59) {
                    local.tm_sec = 0;
                    local.tm_min = minute;
                    local.tm_hour = hour;
                }
            }
        }
    }

    echttp_content_type_json ();
    return housetrace_history(&local);
}

void housetrace_initialize (const char *name, int argc, const char **argv) {
    int i;
    char uri[256];
    const char *folder;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-traces=", argv[i], &folder))
            TraceFolder = folder;
    }
    if (name) TraceName = strdup(name);
    gethostname (LocalHost, sizeof(LocalHost));

    snprintf (uri, sizeof(uri), "/%s/traces", TraceName);
    echttp_route_uri (strdup(uri), housetrace_webhistory);
    snprintf (uri, sizeof(uri), "/%s/traces/files", TraceName);
    echttp_static_route (strdup(uri), TraceFolder);

    // Alternate paths for application-independent web pages.
    // (The trace files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/traces", housetrace_webhistory);
    echttp_static_route ("/traces/files", TraceFolder);
}

void housetrace_periodic (time_t now) {

    static time_t LastCleanup = 0;

    if (now > LastCleanup + 10) {
        if (TraceFile) {
            fclose (TraceFile);
            TraceFile = 0;
        }
        LastCleanup = now;
    }
}

