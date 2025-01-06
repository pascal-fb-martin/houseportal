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
 * housedepositorstate.h - Client to backup and restore application state.
 */
typedef void BackupListener (void);
void housedepositor_state_listen (BackupListener *listener);

typedef int BackupWorker (char *buffer, int size);
void housedepositor_state_register (BackupWorker *worker);

void housedepositor_state_load (const char *app, int argc, const char **argv);

void housedepositor_state_share (int on);
long housedepositor_state_get (const char *path);
const char *housedepositor_state_get_string (const char *path);
void housedepositor_state_changed (void);

void housedepositor_state_background (time_t now);

