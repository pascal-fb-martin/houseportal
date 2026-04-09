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
 * housecontrol.h - Client interface with the control servers.
 */

typedef void ControlTrigger (const char *name,
                             long long timestamp,
                             const char *old, const char *new);

void housecontrol_subscribe (const char *gear, ControlTrigger *trigger);

void housecontrol_sampling (int period);

int housecontrol_ready (void);

const char *housecontrol_state  (const char *name);

int housecontrol_set     (const char *name, const char *state,
                          int pulse, int manual, const char *reason);
int housecontrol_start   (const char *name,
                          int pulse, int manual, const char *reason);
void housecontrol_cancel (const char *name, const char *reason);

int housecontrol_status (char *buffer, int size);
void housecontrol_background (time_t now);

