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
 * hp_redirect.c - The houseportal's HTTP request redirector.
 *
 * SYNOPSYS:
 *
 * void hp_redirect_start (int argc, const char **argv);
 *
 *    Initialize the HTTP request redirector.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <arpa/inet.h>

#include <time.h>
#include <stdio.h>

#include "houseportal.h"


static int RestrictUdp2Local = 0;

typedef struct {
    char *path;
    char *target;
    int length;
    int truncate;
    time_t expiration;
} HttpRedirection;

#define REDIRECT_MAX 1024
#define REDIRECT_LIFETIME 600

static int RedirectionCount = 0;
static HttpRedirection Redirections[REDIRECT_MAX];

static const char *HostName = 0;

static const HttpRedirection *SearchBestRedirect (const char *path) {

    int i;
    int quality = 0;
    time_t now = time(0);
    HttpRedirection *found = 0;

    if (path[0] == 0 || path[1] == 0) return 0;

    for (i = 0; i < RedirectionCount; ++i) {
        int length = Redirections[i].length;
        time_t expiration = Redirections[i].expiration;

        if (length <= quality) continue;
        if (expiration < now && expiration > 0) continue;
        if (strncmp (Redirections[i].path, path, length)) continue;
        if (path[length] != 0 && path[length] != '/') continue;

        found = Redirections + i;
        quality = length;
    }
    return found;
}

static const char *RedirectRoute (const char *method, const char *uri,
                                  const char *data, int length) {
    const HttpRedirection *r = SearchBestRedirect (uri);
    if (r) {
        static char url[2048]; // Accessed once after return.
        char parameters[1024];
        if (r->truncate) {
            uri += r->length;
            if (uri[0] == 0) uri = "/";
        }
        echttp_parameter_join (parameters, sizeof(parameters));
        if (parameters[0])
           snprintf (url, sizeof(url), "http://%s%s?%s",
                     r->target, uri, parameters);
        else
           snprintf (url, sizeof(url), "http://%s%s", r->target, uri);
        echttp_redirect (url);
    } else {
        echttp_error (500, "Unresolvable redirection.");
    }
    return "";
}


static void AddSingleRedirect (int permanent, int truncate,
                               const char *target, const char *path) {

    int i;
    char buffer[1024];
    time_t expiration = (permanent)?0:time(0)+REDIRECT_LIFETIME;

    if (!strchr(target, ':')) {
       if (!HostName) {
           char hostname[1000];
           gethostname (hostname, sizeof(hostname));
           HostName = strdup(hostname);
       }
       snprintf (buffer, sizeof(buffer), "%s:%s", HostName, target);
       target = buffer;
    }

    // First search if this is just a renewal.
    // It is OK to renew an obsolete entry. A renewal may change everything
    // but the path: target, permanent/live, truncate option, etc..
    //
    for (i = 0; i < RedirectionCount; ++i) {
        if (strcmp (Redirections[i].path, path) == 0) {
            if (strcmp (Redirections[i].target, target)) {
                free (Redirections[i].target);
                Redirections[i].target = strdup(target);
            }
            Redirections[i].truncate = truncate;
            Redirections[i].expiration = expiration;
            return;
        }
    }

    // Not a renewal: use a new slot.
    // Do not reuse an expired slot: echttp does not handle dynamic changes
    // to HTTP routes at this time.
    //
    if (RedirectionCount < REDIRECT_MAX) {
        char *p = strdup(path);
        if (echttp_isdebug())
           printf ("Add %sroute for %s, redirected to %s%s\n",
                   permanent?"permanent ":"", p, target,truncate?" (truncate)":"");
        echttp_route_match (p, RedirectRoute);
        Redirections[RedirectionCount].path = p;
        Redirections[RedirectionCount].target = strdup(target);
        Redirections[RedirectionCount].length = strlen(path);
        Redirections[RedirectionCount].truncate = truncate;
        Redirections[RedirectionCount].expiration = expiration;
        RedirectionCount += 1;
    }
}

static void AddRedirect (int permanent, char **token, int count) {

    int i = 1;
    int truncate = 0;
    const char *target = token[0];

    if (strcmp ("TRUNCATE", token[1]) == 0) {
        truncate = 1;
        i = 2;
    }
    for (; i < count; ++i) {
        AddSingleRedirect (permanent, truncate, target, token[i]);
    }
}

static void LoadConfig (const char *name) {

    int i, start, count, line = 0;
    char buffer[1024];
    char *token[16];

    FILE *f = fopen (name, "r");
    if (f == 0) {
        fprintf (stderr, "cannot access configuration file %s\n", name);
        exit(0);
    }

    while (!feof(f)) {
        buffer[0] = 0;
        fgets (buffer, sizeof(buffer), f);
        if (buffer[0] > ' ') {
            // Split the line
            for (i = start = count = 0; buffer[i] >= ' '; ++i) {
                if (buffer[i] == ' ') {
                    if (count >= 16) {
                        fprintf (stderr, "too many tokens at %s\n", buffer+i);
                        exit(1);
                    }
                    token[count++] = buffer + start;
                    do {
                       buffer[i] = 0;
                    } while (buffer[++i] == ' ');
                    start = i;
                }
            }
            buffer[i] = 0;
            token[count++] = buffer + start;

            if (strcmp("LOCAL", token[0]) == 0) {

                RestrictUdp2Local = 1;

            } else if (strcmp("REDIRECT", token[0]) == 0) {

                if (count < 3) {
                    fprintf (stderr, "incomplete redirect (%d arguments)\n", count);
                    exit(1);
                }
                AddRedirect (1, token+1, count-1);

            } else if (strcmp("SIGN", token[0]) == 0) {
                // TBD

            } else {
                fprintf (stderr, "invalid keyword %s\n", token[0]);
                exit(1);
            }
        }
    }
    fclose(f);
}

void hp_redirect_start (int argc, const char **argv) {

    int i;
    const char *configpath = "/etc/houseportal/houseportal.config";

    for (i = 1; i < argc; ++i) {
        echttp_option_match ("-config=", argv[i], &configpath);
    }

    LoadConfig (configpath);
}

