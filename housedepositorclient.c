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

#include "echttp.h"
#include "houselog.h"
#include "housediscover.h"
#include "housedepositor.h"

const char *Path[10];
int PathCount = 0;

time_t Deadline = 0;

#define DEBUG if (echttp_isdebug()) printf

static void discovered (const char *service, void *context, const char *url) {

    int *first = (int *)context;
    if (*first) {
        printf ("%s:\n", service);
        *first = 0;
    }
    printf ("    %s\n", url);
}

static void background (int fd, int mode) {

    static int Counter = 0;

    time_t now = time(0);

    DEBUG ("background, count %d\n", Counter);

    if (Counter == 1) {
        DEBUG ("Starting the discovery\n");
    }
    Counter += 1;
    housediscover (now);
    housedepositor_periodic(now);
}

static void listener (const char *name, const char *data, int length) {
    printf ("%s: %s\n", name, data);
    exit (0);
}

int main (int argc, const char **argv) {

    const char *option[100];
    int optioncount = 1;
    int i;
    const char *waitlimit;
    int doput = 0;

    option[0] = argv[0];

    Deadline = time(0) + 5;

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            if (PathCount < 10) {
                Path[PathCount++] = argv[i];
            }
        } else if (echttp_option_match("-sleep=", argv[i], &waitlimit)) {
            Deadline = time(0) + atoi(waitlimit);
        } else if (echttp_option_present("-put", argv[i])) {
            doput = 1;
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

    if (PathCount == 2) {
       housedepositor_subscribe (Path[0], Path[1], listener);
    }

    echttp_loop();
    return 0;
}

