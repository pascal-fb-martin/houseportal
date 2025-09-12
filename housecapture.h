/* houseportal - A simple web portal for home servers
 *
 * Copyright 2025, Pascal Martin
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
 * housecapture.h - An IO capture modules
 */
void housecapture_initialize (const char *root,
                              int argc, const char **argv);
int housecapture_register (const char *category);
int housecapture_registered (void);

time_t housecapture_active (int category);

void housecapture_record_timed (const struct timeval *timestamp,
                                int category,
                                const char *object,
                                const char *action,
                                const char *format, ...);

#define housecapture_record(C, O, A, F, ...) housecapture_record_timed (0, C, O, A, F, ##__VA_ARGS__)

void housecapture_background (time_t now);

