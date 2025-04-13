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
 * housedepositorclient.c - A command line client for the HouseDepot service
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "echttp.h"
#include "houselog.h"
#include "housediscover.h"
#include "housedepositor.h"

static const char *Path[10];
static int PathCount = 0;

static int PutRequested = 0;

static time_t Deadline = 0;

#define DEBUG if (echttp_isdebug()) printf

static void putrevision (void) {
    if (PutRequested) return; // Do it only once.
    DEBUG ("Put %s to %s/%s\n", Path[2], Path[0], Path[1]);
    housedepositor_put_file (Path[0], Path[1], Path[2]);
    PutRequested = 1;
}

static void background (int fd, int mode) {

    static time_t FirstCall = 0;
    static time_t LastCall = 0;

    time_t now = time(0);

    if (now > LastCall) {
       if (!FirstCall) FirstCall = now;
       housediscover (0); // Force rapid discovery.
       LastCall = now;
       DEBUG ("=== Background at %d sec.\n", now-FirstCall);
    }
    if (now == FirstCall + 1) {
        static int StartMsg = 0;
        if (!StartMsg) DEBUG ("Starting the discovery\n");
        StartMsg = 1;
    }
    if (PathCount > 2) {
        if (now == FirstCall + 5) {
            putrevision();
        } else if (now == FirstCall + 7) {
            exit(0);
        }
    }
    housedepositor_periodic(now);
    housediscover (now);
}

static void listener (const char *name, time_t timestamp, const char *data, int length) {
    if (PathCount == 2) {
        printf ("%s", data);
        exit (0);
    }
    if (PathCount > 2) {
        putrevision();
    }
}

int main (int argc, const char **argv) {

    const char *option[100];
    int optioncount = 1;
    int i;
    const char *waitlimit;

    option[0] = argv[0];

    Deadline = time(0) + 5;

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            if (PathCount < 10) {
                Path[PathCount++] = argv[i];
            }
        } else if (echttp_option_present("-h", argv[i])) {
            printf ("%s repository name file\n", argv[0]);
            exit (0);
        } else if (echttp_option_match("-sleep=", argv[i], &waitlimit)) {
            Deadline = time(0) + atoi(waitlimit);
        } else {
            if (optioncount < 100) {
                option[optioncount++] =  argv[i];
            }
        }
    }
    if (optioncount < 100) {
        option[optioncount++] = "-http-service=dynamic";
    }

    optioncount = echttp_open (optioncount, option);
    echttp_background (&background);
    houselog_initialize ("discovery", argc, argv);
    housediscover_initialize (optioncount, option);
    housedepositor_initialize (optioncount, option);

    if (PathCount < 2) {
       printf ("No depot file provided.\n");
       exit (1);
    }
    housedepositor_subscribe (Path[0], Path[1], listener);

    // Check that the file provided is legit.
    if (PathCount > 2) {
       struct stat filestat;
       if (stat (Path[2], &filestat)) {
           printf ("File %s does not exist\n", Path[2]);
           exit (1);
       }
       if ((filestat.st_mode & S_IFMT) != S_IFREG) {
           printf ("File %s is not a regular file\n");
           exit (1);
       }
    }

    echttp_loop();
    return 0;
}

