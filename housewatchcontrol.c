/* houseportal - A simple web portal for home servers
 *
 * Copyright 2026, Pascal Martin
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
 * housewatchcontrol.c - A utility to monitor control point changes.
 *
 * SYNOPSYS:
 *
 *   housewatchcontrol [-s=N] gear ..
 *
 * The tool will display changes of any control points associated with
 * a listed gear.
 *
 * The -s option set a sampling rate (milliseconds). The default sampling
 * rate is 10ms.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "echttp.h"

#include "housediscover.h"
#include "houselog.h"

#include "housecontrol.h"

#define DEBUG if (echttp_isdebug()) printf


static void background (int fd, int mode) {

    time_t now = time(0);

    housediscover (now);
    houselog_background (now);
    housecontrol_background (now);
}

static void onchanges (const char *name,
                       long long timestamp,
                       const char *old, const char *new) {
    printf ("Point %s changed from %s to %s at %lld.%03lld\n",
            name, old, new, timestamp/1000, timestamp%1000);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    echttp_background (&background);
    housediscover_initialize (argc, argv);
    houselog_initialize ("watch", argc, argv);

    int i;
    int sampling = 10;

    for (i = 1; i < argc; ++i) {
       const char *value;
       if (echttp_option_match ("-s", argv[i], &value)) {
          sampling = atoi (value);
          continue;
       }
       housecontrol_subscribe (argv[i], onchanges);
    }

    housecontrol_sampling (sampling);

    echttp_loop();
    exit(0);
}

