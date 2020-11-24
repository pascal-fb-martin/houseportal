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
 * houseportalclient.c - The houseportal program's client API.
 */

void houseportal_initialize    (int argc, const char **argv);
void houseportal_signature     (const char *cypher, const char *key);
void houseportal_register      (int webport, const char **paths, int count);
void houseportal_register_more (int webport, const char **paths, int count);
void houseportal_renew         (void);

const char *houseportal_server (void);

int  hp_udp_client (const char *destination, const char *service);
void hp_udp_send   (const char *data, int length);

