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
const char *houseconfig_load (int argc, const char **argv);

const char *houseconfig_current (void);
const char *houseconfig_name (void);
int         houseconfig_open (void); // DEPRECATED.
int         houseconfig_size (void); // DEPRECATED.

const char *houseconfig_update (const char *text);

const char *houseconfig_string  (int parent, const char *path);
int         houseconfig_integer (int parent, const char *path);
int         houseconfig_boolean (int parent, const char *path);

int houseconfig_array        (int parent, const char *path);
int houseconfig_array_length (int array);
int houseconfig_object       (int parent, const char *path);

int houseconfig_array_object (int parent, int index);
