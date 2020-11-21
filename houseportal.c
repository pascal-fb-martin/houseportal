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
 * houseportal.c - Main loop of the houseportal program.
 *
 * SYNOPSYS:
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "houseportal.h"
#include "houselog.h"
#include "echttp_static.h"


static void hp_help (const char *argv0) {

    int i = 1;
    const char *help;

    printf ("%s [-h] [-debug] [-test]%s\n", argv0, echttp_help(0));

    printf ("\nGeneral options:\n");
    printf ("   -h:              print this help.\n");

    printf ("\nHTTP options:\n");
    help = echttp_help(i=1);
    while (help) {
        printf ("   %s\n", help);
        help = echttp_help(++i);
    }
    exit (0);
}

static const char *hp_portal_list (const char *method, const char *uri,
                                   const char *data, int length) {
    static char buffer[8192];

    buffer[0] = 0;
    hp_redirect_list_json (0, buffer, sizeof(buffer));
    echttp_content_type_json ();
    return buffer;
}

static const char *hp_portal_peers (const char *method, const char *uri,
                                    const char *data, int length) {
    static char buffer[8192];

    buffer[0] = 0;
    hp_redirect_peers_json (buffer, sizeof(buffer));
    echttp_content_type_json ();
    return buffer;
}

static const char *hp_portal_service (const char *method, const char *uri,
                                      const char *data, int length) {
    static char buffer[8192];

    const char *name = echttp_parameter_get ("name");

    buffer[0] = 0;
    if (name)
        hp_redirect_service_json (name, buffer, sizeof(buffer));
    else
        hp_redirect_list_json (1, buffer, sizeof(buffer));
    echttp_content_type_json ();
    return buffer;
}

static void hp_background (int fd, int mode) {
    time_t now = time(0);
    houselog_background (now);
    hp_redirect_background();
}

static void hp_portal_protect (const char *method, const char *uri) {

    const char *origin = echttp_attribute_get ("Origin");
    if (!origin) return; // Not a cross-domain request.

    if (!strcmp (method, "GET")) {
        echttp_attribute_set ("Access-Control-Allow-Origin", "*");
        return;
    }
    if (!strcmp (method, "OPTIONS")) {
        echttp_attribute_set ("Access-Control-Allow-Origin", "*");
        echttp_error (204, "No Content"); // Not an error, but don't process.
        return;
    }
    echttp_error (403, "Forbidden Cross-Domain");
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_present("-h", argv[i])) {
            hp_help(argv[0]);
        }
    }

    echttp_open (argc, argv);
    houselog_initialize ("portal", argc, argv);
    echttp_protect (0, hp_portal_protect);
    echttp_route_uri ("/portal/list", hp_portal_list);
    echttp_route_uri ("/portal/peers", hp_portal_peers);
    echttp_route_uri ("/portal/service", hp_portal_service);
    echttp_static_route ("/", "/usr/local/share/house/public");
    hp_redirect_start (argc, argv);
    echttp_background (&hp_background);
    houselog_trace (HOUSE_INFO, "HousePortal", "Started");
    echttp_loop();
}

