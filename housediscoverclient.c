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
 * housediscoverclient.c - A command line client for the houseportal discovery.
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "echttp.h"
#include "houselog.h"
#include "housediscover.h"

const char *Services[100];
int ServiceCount = 0;

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
    int first;
    int i;

    DEBUG ("background, count %d\n", Counter);

    if (Counter == 1) {
        DEBUG ("Starting the discovery\n");
    }
    Counter += 1;
    housediscover (now);

    if (now > Deadline) {
        first = 1;
        housediscovered ("portal", &first, discovered);

        for (i = 0; i < ServiceCount; ++i) {
            first = 1;
            housediscovered (Services[i], &first, discovered);
        }
        exit(0);
    }
}

int main (int argc, const char **argv) {

    const char *option[100];
    int optioncount = 1;
    int i;

    option[0] = argv[0];

    Deadline = time(0) + 5;

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            if (ServiceCount < 100) {
                Services[ServiceCount++] = argv[i];
            }
        } else if (strncmp (argv[i], "-sleep=", 7) == 0) {
            Deadline += atoi(argv[i]+7) - 5;
        } else {
            if (optioncount < 100) {
                option[optioncount++] =  argv[i];
            }
        }
    }

    if (optioncount < 100) {
        option[optioncount++] = "-http-service=dynamic";
    }

    echttp_open (optioncount, option);
    echttp_background (&background);
    houselog_initialize ("discovery", argc, argv);
    housediscover_initialize (optioncount, option);

    echttp_loop();
    return 0;
}

