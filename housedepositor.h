/* houseportal - A simple web portal for home servers
 *
 * Copyright 2020, Pascal Martin
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
 * housedepositor.h - The generic client side of the HouseDepot service.
 */
void housedepositor_default (const char *arg);
void housedepositor_initialize (int argc, const char **argv);

typedef void housedepositor_listener (const char *name, time_t timestamp,
                                      const char *data, int length);

void housedepositor_subscribe (const char *repository,
                               const char *name,
                               housedepositor_listener *listener);

void housedepositor_put (const char *repository,
                         const char *name,
                         const char *data, int size);

void housedepositor_put_file (const char *repository,
                              const char *name,
                              const char *filename);

void housedepositor_periodic (time_t now);

