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
 *
 * housemech_almanac.h - Client interface with the almanac services.
 */
int    housealmanac_tonight_ready (void);
time_t housealmanac_tonight_sunset (void);
time_t housealmanac_tonight_sunrise (void);
const char *housealmanac_tonight_provider (void);
const char *housealmanac_tonight_origin (void);
int         housealmanac_tonight_priority (void);

int    housealmanac_today_ready (void);
time_t housealmanac_today_sunset (void);
time_t housealmanac_today_sunrise (void);
const char *housealmanac_today_provider (void);
const char *housealmanac_today_origin (void);
int         housealmanac_today_priority (void);

void housealmanac_background (time_t now);
int  housealmanac_status (char *buffer, int size);

