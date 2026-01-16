/* HouseConfig - A simple API for reading a JSON configuration file.
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
 */

void        houseconfig_default (const char *arg);

typedef const char *ConfigListener (void);
const char *houseconfig_initialize (const char *name, ConfigListener *update,
                                    int argc, const char **argv);

int         houseconfig_active (void);
const char *houseconfig_current (void);
const char *houseconfig_name (void);

const char *houseconfig_update (const char *text, const char *reason);
const char *houseconfig_save   (const char *text, const char *reason);

int houseconfig_find (int parent, const char *path, int type);

const char *houseconfig_string  (int parent, const char *path);
int         houseconfig_integer (int parent, const char *path);
int         houseconfig_positive (int parent, const char *path);
int         houseconfig_boolean (int parent, const char *path);

int houseconfig_array        (int parent, const char *path);
int houseconfig_array_length (int array);

int houseconfig_enumerate    (int parent, int *index, int size);

int houseconfig_object       (int parent, const char *path);

void houseconfig_background (time_t now);

