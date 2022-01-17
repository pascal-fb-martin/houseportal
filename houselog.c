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
 * houselog.c - A generic application log recording modules.
 *
 * SYNOPSYS:
 *
 * void houselog_initialize (const char *application,
 *                           const char *portal,
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
 * void houselog_background (time_t now);
 *
 *    This function must be called a regular intervals for background
 *    processing, e.g. cleanup of expired resources, file backup, etc.
 *
 * const char *houselog_host (void);
 *
 *    Return the name of the local machine, as used in the logs.
 *
 * In order to avoid writing frequently to SD cards, the active logs are
 * written to /dev/shm, moved to permanent storage at the end of the day.
 * To limit loss of data on power outage, the logs are also saved to
 * permanent storage every hour.
 *
 * When the application starts, the module tries to backup any existing
 * log file in /dev/shm and then restore any existing file from permanent
 * storage to /dev/shm.
 *
 * Any of these file operations is performed only if the source is more
 * recent than the destination.
 */

#include <unistd.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include "echttp.h"
#include "echttp_static.h"
#include "houselog.h"

static const char *LogTypes = "et";
static const char *LogFolder = "/var/lib/house/log";
static const char *LogName = "portal";

static char LocalHost[256] = {0};
static char PortalHost[256] = {0};

struct EventHistory {
    struct timeval timestamp;
    char   category[32];
    char   object[32];
    char   action[16];
    char   description[128];
};

#define HISTORY_DEPTH 256

static struct EventHistory History[HISTORY_DEPTH];
static int HistoryCursor = 0;
static long HistoryLatestId = 0;

FILE *EventFile = 0;
FILE *TraceFile = 0;


static void houselog_backup (char id, const char *method);

static const char *houselog_temp (char id) {

    static char buffer[512];

    snprintf (buffer, sizeof(buffer), "/dev/shm/house%s_%c.csv", LogName, id);
    return buffer;
}

static const char *houselog_name (const struct tm *local, char id, int write) {

    static char buffer[512];
    char path[256];

    snprintf (path, sizeof(path), "%s/%04d/%02d/%02d",
              LogFolder,
              local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);

    if (write) {
        snprintf (buffer, sizeof(buffer), "/bin/mkdir -p %s", path);
        system (buffer);
    }

    snprintf (buffer, sizeof(buffer), "%s/%s_%c_%04d%02d%02d.csv",
              path, LogName, id,
              local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
    return buffer;
}

static FILE *houselog_update (const struct tm *local, char id) {

    const char *name = houselog_temp (id);

    FILE *fd = fopen (name, "a");
    if (fd == 0) {
        fprintf (stderr, "%s: cannot open, %s\n", name, strerror (errno));
        return 0;
    }

    if (ftell(fd) <= 0) {
        const char *head;
        switch (id) {
        case 't': head = "TIMESTAMP,LEVEL,FILE,LINE,OBJECT,DESCRIPTION"; break;
        case 'e': head = "TIMESTAMP,CATEGORY,OBJECT,ACTION,DESCRIPTION"; break;
        default: return 0;
        }
        fprintf (fd, "%s\n", head);
    }
    return fd;
}

static FILE *houselog_access (const struct tm *local, char id) {

    const char *name = houselog_name (local, id, 0);

    FILE *fd = fopen (name, "r");
    if (fd == 0) {
        fprintf (stderr, "%s: cannot open, %s\n", name, strerror (errno));
        return 0;
    }
    return fd;
}

static void safecpy (char *t, const char *s, int size) {
    strncpy (t, s, size);
    t[size-1] = 0;
}

static void houselog_updated (void) {

    if (HistoryLatestId == 0) {
        // Seed the latest event ID based on the first event's time.
        // This makes it random enough to make its value change after
        // a restart.
        HistoryLatestId = (long) (time(0) & 0xffff);
    }
    HistoryLatestId += 1;
}

void houselog_trace (const char *file, int line, const char *level,
                     const char *object,
                     const char *format, ...) {

    static struct tm Last = {0};

    va_list ap;
    char text[1024];
    struct timeval timestamp;

    gettimeofday (&timestamp, 0);

    va_start (ap, format);
    vsnprintf (text, sizeof(text), format, ap);
    va_end (ap);

    struct tm local = *localtime (&(timestamp.tv_sec));

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
        TraceFile = houselog_update (&local, 't');

    if (echttp_isdebug())
        printf ("%s %s, %d: %s %s\n", level, file, line, object, text);

    if (TraceFile) {
        fprintf (TraceFile, "%ld.%03d,\"%s\",\"%s\",%d,\"%s\",\"%s\"\n",
                 (long) (timestamp.tv_sec), (int)(timestamp.tv_usec/1000),
                 level, file, line, object, text);
        fflush (TraceFile);
    }
    houselog_updated();
}

void houselog_event (const char *category,
                     const char *object,
                     const char *action,
                     const char *format, ...) {

    static struct tm Last = {0};
    va_list ap;
    char *text;
    struct EventHistory *cursor = History + HistoryCursor;

    va_start (ap, format);
    vsnprintf (cursor->description, sizeof(cursor->description), format, ap);
    va_end (ap);

    gettimeofday (&(cursor->timestamp), 0);

    safecpy (cursor->category, category, sizeof(cursor->category));
    safecpy (cursor->object, object, sizeof(cursor->object));
    safecpy (cursor->action, action, sizeof(cursor->action));
    text = cursor->description;

    HistoryCursor += 1;
    if (HistoryCursor >= HISTORY_DEPTH) HistoryCursor = 0;
    History[HistoryCursor].timestamp.tv_sec = 0;

    struct tm local = *localtime (&(cursor->timestamp.tv_sec));

    if (local.tm_year != Last.tm_year ||
        local.tm_mon != Last.tm_mon ||
        local.tm_mday != Last.tm_mday) {

        if (EventFile) {
            fclose (EventFile);
            EventFile = 0;
        }
        Last = local;
    }

    if (!EventFile)
        EventFile = houselog_update (&local, 'e');

    if (EventFile) {
        fprintf (EventFile, "%ld.%03d,\"%s\",\"%s\",\"%s\",\"%s\"\n",
                 (long) (cursor->timestamp.tv_sec),
                 (int)(cursor->timestamp.tv_usec/1000),
                 category, object, action, text);
    }
    houselog_updated();
}

static int houselog_getheader (time_t now, char *buffer, int size) {

    if (PortalHost[0]) {
        return snprintf (buffer, size,
                        "{\"host\":\"%s\",\"proxy\":\"%s\",\"apps\":[\"%s\"],"
                            "\"timestamp\":%ld,\"%s\":{\"latest\":%ld",
                        LocalHost, PortalHost, LogName,
                            (long)now, LogName, HistoryLatestId);
    }
    return snprintf (buffer, size,
                    "{\"host\":\"%s\",\"apps\":[\"%s\"],"
                        "\"timestamp\":%ld,\"%s\":{\"latest\":%ld",
                    LocalHost, LogName,
                        (long)now, LogName, HistoryLatestId);
}

static const char *houselog_get (struct tm *local, const char *id) {

    static char buffer[65537];

    time_t start = mktime(local);
    int length;
    char *eol;
    const char *prefix = "";

    length = houselog_getheader (time(0), buffer, sizeof(buffer));
    length += snprintf (buffer+length, sizeof(buffer)-length, ",\"%s\":[", id);

    houselog_backup (id[0], "cp"); // Make sure the file is up-to-date.
    FILE *fd = houselog_access (local, id[0]);

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

static const char *houselog_live (time_t now) {

    static char buffer[128+HISTORY_DEPTH*(sizeof(struct EventHistory)+24)] = {0};

    const char * prefix = "";
    int length;
    int i;

    length = houselog_getheader (now, buffer, sizeof(buffer));
    length += snprintf (buffer+length, sizeof(buffer)-length, ",\"events\":[");

    for (i = HistoryCursor + 1; i != HistoryCursor; ++i) {
        if (i >= HISTORY_DEPTH) {
            i = 0;
            if (!HistoryCursor) break;
        }
        struct EventHistory *cursor = History + i;

        if (!(cursor->timestamp.tv_sec)) continue;

        int wrote = snprintf (buffer+length, sizeof(buffer)-length,
                              "%s[%ld%03d,\"%s\",\"%s\",\"%s\",\"%s\"]",
                              prefix,
                              (long)(cursor->timestamp.tv_sec),
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
    }
    snprintf (buffer+length, sizeof(buffer)-length, "]}}");
    return buffer;
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

    // The default is to show the most recent data.
    // To force a specific start time, use the "time" parameter.
    // For getting data from another day, set the "date" parameter.
    // Both are local time relative to the server.
    //
    time_t now = time(0);

    const char *date = echttp_parameter_get ("date"); // YYYY-MM-DD
    const char *hhmm = echttp_parameter_get ("time"); // HH:MM

    time_t start = now - 600; // Default: show the last 10 minutes.
    struct tm local = *localtime (&start);

    const char *id;

    if (strstr (uri, "/events")) id = "events";
    else if (strstr (uri, "/traces")) id = "traces";
    else {
        echttp_error (404, "unsupported data type");
        return "";
    }

    echttp_content_type_json ();

    if (id[0] == 'e' && !date && !hhmm) {
        return houselog_live (now);
    }

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
                    cursor = strchr (cursor, '-');
                    if (cursor) {
                        if (cursor[1] == '0') ++cursor;
                        day = atoi(++cursor);
                        if (day >= 1 && day <= 31) {
                            local.tm_year = year - 1900;
                            local.tm_mon  = month - 1;
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

    return houselog_get (&local, id);
}

static void houselog_backup (char id, const char *method) {
    struct stat buffer;
    const char *tempname = houselog_temp (id);

    if (stat (tempname, &buffer) == 0) {

        char command[1024];
        struct tm oldfile = *localtime (&(buffer.st_ctim.tv_sec));
        const char *archivename = houselog_name (&oldfile, id, 1);

        snprintf (command, sizeof(command),
                  "/bin/%s -f -u %s %s", method, tempname, archivename);
        system (command);
    }
}

static void houselog_restore (const struct tm *local, char id) {
    struct stat buffer;
    const char *tempname = houselog_temp (id);

    houselog_backup (id, "mv"); // Backup any existing shm file first.

    const char *archivename = houselog_name (local, id, 0);

    if (stat (archivename, &buffer) == 0) {
        char command[1024];
        snprintf (command, sizeof(command),
                  "/bin/cp -u %s %s", archivename, tempname);
        system (command);
    }
}

void houselog_initialize (const char *name,
                          const char *portal, int argc, const char **argv) {
    int i;
    char uri[256];
    const char *folder;

    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-log=", argv[i], &folder))
            LogFolder = folder;
    }
    if (name) LogName = strdup(name);
    gethostname (LocalHost, sizeof(LocalHost));
    if (portal) snprintf (PortalHost, sizeof(PortalHost), "%s", portal);

    snprintf (uri, sizeof(uri), "/%s/log/traces", LogName);
    echttp_route_uri (strdup(uri), houselog_webget);

    snprintf (uri, sizeof(uri), "/%s/log/events", LogName);
    echttp_route_uri (strdup(uri), houselog_webget);

    snprintf (uri, sizeof(uri), "/%s/log/latest", LogName);
    echttp_route_uri (strdup(uri), houselog_weblatest);

    snprintf (uri, sizeof(uri), "/%s/log/files", LogName);
    snprintf (uri, sizeof(uri), "/%s/log/files", LogName);
    echttp_static_route (strdup(uri), LogFolder);

    // Alternate paths for application-independent web pages.
    // (The log files are stored at the same place for all applications.)
    //
    echttp_route_uri ("/log/traces", houselog_webget);
    echttp_route_uri ("/log/events", houselog_webget);
    echttp_static_route ("/log/files", LogFolder);

    time_t now = time(0);
    struct tm local = *localtime (&now);
    for (i = 0; LogTypes[i] > 0; ++i) {
        houselog_restore (&local, LogTypes[i]);
    }
    houselog_background (now); // Initial state.

    houselog_event ("SERVICE", LogName, "STARTING", "ON %s", LocalHost);
}

void houselog_background (time_t now) {

    static int LastDay = 0;
    static int LastHour = 0;
    static time_t LastCleanup = 0;

    int i;
    struct tm local = *localtime (&now);

    if (LastCleanup == 0) {
        LastDay = local.tm_mday;
        LastHour = local.tm_hour;
        LastCleanup = now;
        return;
    }

    if (now > LastCleanup + 10) {
        if (TraceFile) {
            fclose (TraceFile);
            TraceFile = 0;
        }
        if (EventFile) {
            fclose (EventFile);
            EventFile = 0;
        }
        LastCleanup = now;

        if (local.tm_mday != LastDay) {

            for (i = 0; LogTypes[i] > 0; ++i) {
                houselog_backup (LogTypes[i], "mv");
            }
            LastDay = local.tm_mday;
            LastHour = local.tm_hour;

        } else if (local.tm_hour != LastHour) {

            for (i = 0; LogTypes[i] > 0; ++i) {
                houselog_backup (LogTypes[i], "cp");
            }
            LastHour = local.tm_hour;
        }
    }
}

const char *houselog_host (void) {
    return LocalHost;
}

