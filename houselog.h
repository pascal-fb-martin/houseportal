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
 * houselog.h - A generic application log recording modules.
 *
 * houselog_trace() should be used with the predefined constant, as in:
 *    houselog_trace (HOUSE_INFO, "myobject", "my message is %s", "this");
 */
#include <time.h>

void houselog_initialize (const char *application,
                            int argc, const char **argv);

void houselog_trace (const char *file, int line, const char *level,
                     const char *object,
                     const char *format, ...);

#define HOUSE_INFO    __FILE__, __LINE__, "INFO"
#define HOUSE_WARNING __FILE__, __LINE__, "WARN"
#define HOUSE_FAILURE __FILE__, __LINE__, "FAIL"

void houselog_event (time_t timestamp,
                     const char *category,
                     const char *object,
                     const char *action,
                     const char *format, ...);

void houselog_background (time_t now);
