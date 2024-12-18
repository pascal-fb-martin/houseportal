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
 * houselog_sensor.h - An application log recording modules (sensor data).
 */
void houselog_sensor_initialize (const char *application,
                                 int argc, const char **argv);

void houselog_sensor_data (const struct timeval *timestamp,
                           const char *location, const char *name,
                           const char *value, const char *unit);

void houselog_sensor_numeric (const struct timeval *timestamp,
                              const char *location, const char *name,
                              long long value, const char *unit);

void houselog_sensor_flush (void);

void houselog_sensor_background (time_t now);

