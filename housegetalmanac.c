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
 * housegetalmanac.c - A command line client for the almanac services
 *
 * This program is intended for testing and troubleshooting purpose only.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "echttp.h"
#include "houselog.h"
#include "housediscover.h"
#include "housealmanac.h"

int ServiceCount = 0;

time_t Deadline = 0;

#define DEBUG if (echttp_isdebug()) printf

static void background (int fd, int mode) {

    static int Counter = 0;

    time_t now = time(0);

    DEBUG ("background, count %d\n", Counter);

    if (Counter == 1) {
        DEBUG ("Starting the discovery\n");
    }
    Counter += 1;
    housediscover (now);
    housealmanac_background (now);

    if (now > Deadline) {
        if (! housealmanac_ready()) {
            printf ("No almanac service detected.\n");
        } else {
            printf ("Almanac service: %s (priority %d)\n",
                    housealmanac_provider(), housealmanac_priority());
            time_t t = housealmanac_sunset();
            printf ("Sunset: %s", ctime (&t));
            t = housealmanac_sunrise();
            printf ("Sunrise: %s", ctime (&t));
        }
        exit(0);
    }
}

int main (int argc, const char **argv) {

    const char *option[100];
    int optioncount = 1;
    int i;
    time_t start = time(0);

    option[0] = argv[0];

    Deadline = start + 5;

    for (i = 1; i < argc; ++i) {
        if (strncmp (argv[i], "-sleep=", 7) == 0) {
            Deadline = start + atoi(argv[i]+7);
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

    echttp_loop();
    return 0;
}

