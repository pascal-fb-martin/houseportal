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
 * housestate.c - A small component to manage state changes.
 *
 * OVERVIEW:
 *
 * The purpose of this small module is to provide a basic mechanism to
 * lower the overhead caused by periodic state polling. The design
 * is as follow:
 *
 * - The server maintains a state ID, which only property is that its
 *   value changes whenever the internal state of the server changes.
 *   This state ID is used by the client to detect changes.
 *
 * - The client polls a first time with no specific argument. It receives
 *   JSON data that include the server internal state and the state ID.
 *
 * - The client's uses this state ID as the "known" argument in its
 *   subsequent poll.
 *
 * - If the state ID has changed compare to this "known" argument,
 *   the server returns HTTP code 304 (not modified) and no data.
 *
 * - If the state ID has changed compare to this "known" argument,
 *   the server returns the new internal state data, including the
 *   current state value.
 *
 * - The client updates the data shown on the page, and its "known" state
 *   ID value.
 *
 * A server can hold multiple internal states, for example one state
 * represents its current configuration and another its current live data.
 *
 * SYNOPSIS:
 *
 * int housestate_declare (const char *name);
 *
 *   Declare a new state context. This returns a handle to the new state
 *   context. If the state context already exists, its is reused and no
 *   new state context is created.
 *
 * void housestate_cascade (int parent, int child);
 *
 *   Create a cascade dependency between a child and its parent: if
 *   the parent value is changed, the value of all its children are changed.
 *   A parent can have multiple children, a child only has one parent.
 *
 *   The relationship is transitive: if A is the parent of B and B is the
 *   parent of C, a change to A causes a change to B, and the change to B
 *   causes a change to C.
 *
 *   A typical situation is the relationship between configuration and live
 *   data: a configuration change typically impacts the live data to be shown.
 *
 * void housestate_changed (int handle);
 *
 *   Trigger a state change.
 *
 * int housestate_same (int handle);
 *
 *   Detect state change compare to a known value. The known value is
 *   retrieved as the HTTP parameter named "known".
 *
 *   Returns 0 if the state has changed since, or 1 if there was no change.
 *   If there was no change, this function also trigger HTTP code 304 and
 *   the calling HTTP endpoint shall immediately return without processing
 *   the client's request.
 *
 * unsigned long housestate_current (int handle);
 *
 *   Return the current value of the state ID.
 *
 * DEPENDENCIES:
 *
 * This module: depends on the presence of the "known" HTTP argument and
 * generates the HTTP code 304 when appropriate.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "echttp.h"
#include "housestate.h"

struct HouseStateContext {
   char name[8];
   unsigned long value; // No overflow for a long time..
   int child;
   int parent;
   int next;
};

#define HOUSESTATE_SIZE 8 // Hardcoded size for now.

static struct HouseStateContext HouseStates[HOUSESTATE_SIZE];
static int                      HouseStatesCount = 0;

int housestate_declare (const char *name) {

   int i;
   for (i = 0; i < HouseStatesCount; ++i) {
      if (!strcmp (name, HouseStates[i].name)) return i;
   }

   if (HouseStatesCount >= HOUSESTATE_SIZE) return -1;

   // This is a new state ID and there is room for it.
   //
   snprintf (HouseStates[HouseStatesCount].name, sizeof (HouseStates[0].name),
             "%s", name);
   HouseStates[HouseStatesCount].child = -1;
   HouseStates[HouseStatesCount].parent = -1;
   HouseStates[HouseStatesCount].next = -1;

   // Set the initial state with a somewhat random value, so that the clients
   // can detect a restart.
   HouseStates[HouseStatesCount].value = (long)(time(0) & 0xffff) * 100;

   return HouseStatesCount++;
}

static int housestate_heir (int parent, int child) {
   int i;
   for (i = HouseStates[parent].child; i >= 0; i = HouseStates[i].next) {
      if (i == child) return 1;
      if (housestate_heir (i, child)) return 1;
   }
   return 0;
}

void housestate_cascade (int parent, int child) {

   if ((parent < 0) || (parent >= HouseStatesCount)) return;
   if ((child < 0) || (child >= HouseStatesCount)) return;

   // Reject cases that would cause a circular dependency, at least
   // the most obvious ones.
   if (parent == child) return; // Makes no sense.
   if (HouseStates[child].parent >= 0) return; // In a list already.
   if (housestate_heir (child, parent)) return; // Transitively circular.

   HouseStates[child].next = HouseStates[parent].child;
   HouseStates[child].parent = parent;
   HouseStates[parent].child = child;
}

void housestate_changed (int handle) {

   if ((handle < 0) || (handle >= HouseStatesCount)) return;

   HouseStates[handle].value += 1;

   // Propagate along the cascade.
   int child;
   for (child = HouseStates[handle].child;
        child >= 0; child = HouseStates[child].next) {
      housestate_changed (child);
   }
}

int housestate_same (int handle) {

   if ((handle < 0) || (handle >= HouseStatesCount)) return 0;

   // We use atoll() here to avoid overflow issues.
   //
   const char *knownpar = echttp_parameter_get("known");
   if (knownpar && (atoll(knownpar) == HouseStates[handle].value)) {
      echttp_error (304, "Not Modified");
      return 1;
   }
   return 0; // Not the same as what was known already.
}

unsigned long housestate_current (int handle) {
   if ((handle < 0) || (handle >= HouseStatesCount)) return 0;
   return HouseStates[handle].value;
}

