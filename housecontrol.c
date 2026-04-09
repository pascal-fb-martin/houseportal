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
 * housecontrol.c - Client interface with the control servers.
 *
 * SYNOPSYS:
 *
 * This module handles detection of, and communication with, the control
 * servers:
 * - Run periodic discoveries to find which server handles each control.
 * - Handle the HTTP control requests (and redirects).
 * - Periodic poll of control points and state change detection.
 *
 * Each control is independent of each other.
 *
 * This module remembers which controls are active, so that it does not
 * have to stop every known control on cancel.
 *
 * In this module the term "uri" (or "URI") means the path that identifies
 * a unique House service, but does not identifies an exact endpoint.
 * The term "url" means the HTTP path to an exact endpoint on a unique service.
 * 
 * typedef void ControlTrigger (const char *name,
 *                              long long timestamp,
 *                              const char *old, const char *new);
 *
 * void housecontrol_subscribe (const char *gear,
 *                                   ControlTrigger *trigger);
 *
 *    Declare a trigger function to be called when the control's state changes.
 *    This is also called when the control is initially discovered, in which
 *    case the old state is an empty string.
 *    The trigger can be specific for each type of gear, or be a default
 *    trigger (when gear is null or "*"). That default trigger is used
 *    only when no matching gear-specific trigger was declared.
 *
 *    A trigger declared here can be disabled by declaring a null trigger
 *    for the same gear.
 *
 *    If the application does not care about the type of gear, it just needs
 *    to declare a default trigger.
 *
 * void housecontrol_sampling (int period);
 *
 *    This function sets a requested sampling period for input points.
 *    The period value is in milliseconds. Values above 1000 will be ignored.
 *
 *    A side effect of setting a sampling period is that it will cause this
 *    module to use the history extension, i.e history polling, whenever
 *    available.
 *
 *    Setting the sampling period to 0 effectively disables high speed
 *    sampling and causes a return to standard polling. This is a way
 *    for the application to enable high speed sampling only when required
 *    by the application logic.
 *
 *    Note that the effect of changing the sampling period is not immediate.
 *
 *    By default this module uses standard polling, and the effective sampling
 *    period is between 1 and 2 seconds.
 *
 *    This sampling period request should be considered more like a hint than
 *    a command. The actual period used in practice may not match the requested
 *    value, for two reasons:
 *    - not all providers support the history extension required to support
 *      high speed sampling.
 *    - even providers that support the history extension may not support
 *      the specific requested sampling period.
 *
 * int housecontrol_ready (void);
 *
 *    Return 1 if at least one control point is known, 0 otherwise.
 *    The purpose is to delay rules execution until at least one control
 *    service has been detected.
 *
 * const char *housecontrol_state (const char *name);
 *
 *    Get the latest known state of a control. States values are typically
 *    "on", "off" or "alert", but other values are possible.
 *
 *    This returns 0 if the named control is not known on any server.
 *    This returns "" if the named control is known, but not its value.
 *
 * void housecontrol_set (const char *name, const char *state,
 *                        int pulse, int manual, const char *reason);
 *
 *    Set a control to the specified state for the duration of the pulse.
 *    The reason typically indicates what triggered this control. The manual
 *    parameter controls both the local generation of an event and the
 *    priority of the new state.
 *
 *    If the named control is not known on any server, the request is ignored.
 *
 * int housecontrol_start (const char *name,
 *                         int pulse, int manual, const char *reason);
 *
 *    This function is equivalent to housecontrol_set() with state "on".
 *
 *    If the named control is not known on any server, the request is ignored.
 *
 * void housecontrol_cancel (const char *name, const char *reason);
 *
 *    Immediately stop a control, or all active controls if name is null.
 *    To stop a control means to set it to "off".
 *
 *    Not that using a null name only cancels those control initiated by
 *    this program, not all known controls.
 *
 * void housecontrol_background (time_t now);
 *
 *    The periodic function that detects the control servers.
 *
 * int housecontrol_status (char *buffer, int size);
 *
 *    Return the status of control points in JSON format.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_hash.h>
#include <echttp_json.h>
#include <echttp_encoding.h>

#include "houselog.h"
#include "housediscover.h"

#include "housecontrol.h"

#define DEBUG if (echttp_isdebug()) printf

typedef struct {
    const char *uri;  // The unique HTTP path identifying this provider
    unsigned int signature;
    int has_history;
    time_t detected;  // When it was reported last by the discovery.
    time_t replied;   // When a response was last received (even status 304)
    long long known;  // Optimization to detect changes. See housestate.c
    long long since;   // Next history start time.
} ControlProvider;

static echttp_hash ProvidersCatalog;

static ControlProvider *Providers = 0;
static int              ProvidersCount = 0;
static int              ProvidersAllocated = 0;

typedef struct {
    const char *gear;
    unsigned int signature;
    ControlTrigger *action;
} HouseTrigger;

static HouseTrigger *Triggers = 0;
static int           TriggersCount = 0;
static int           TriggersSize = 0;

typedef struct {
    const char *name;
    unsigned int signature;
    char *gear;
    ControlTrigger *trigger;
    long long timestamp;
    char state[8];
    char status; // u: unmapped, i: idle, a: active (pending).
    char type;   // i: input, o: output;
    time_t deadline;
    char uri[256];
} HouseControl;

static HouseControl *Controls = 0;
static int           ControlsCount = 0;
static int           ControlsSize = 0;

static int ControlRequestedSampling = 0;
static int ControlsActive = 0;


static void housecontrol_noop (const char *name,
                                    long long timestamp,
                                    const char *old, const char *new) {
    // Nothing is done on purpose.
}

static void housecontrol_trigger (HouseControl *control,
                                       long long timestamp,
                                       const char *old, const char *new) {

    if (control->trigger) {
        // The trigger for this control was already identified.
        control->trigger (control->name, timestamp, old, new);
        return;
    }

    const char *gear = control->gear;
    if (!gear) gear = "*";

    // Search for an applicable trigger, either the trigger for the specific
    // control's gear type, or else the specified default one (gear '*').
    //
    unsigned int signature = echttp_hash_signature (gear);
    ControlTrigger *trigger = housecontrol_noop; // Factory default.
    int i;
    for (i = 0; i < TriggersCount; ++i) {
        if (Triggers[i].gear[0] == '*') {
            // This trigger, if set, becomes the default.
            if (Triggers[i].action) trigger = Triggers[i].action;
        }
        if (Triggers[i].signature != signature) continue; // Accelerator.
        if (strcasecmp (control->gear, Triggers[i].gear)) continue; // No match.

        // We found a match: get the trigger (if set) and stop the search.
        // This trigger may have been disabled, in which case the search
        // must continue for the explicit default, if any.
        //
        if (Triggers[i].action) trigger = Triggers[i].action;
        if (trigger != housecontrol_noop) break; // Explicit trigger found
    }
    control->trigger = trigger; // Remember for faster processing next time.

    trigger (control->name, timestamp, old, new);
}

static void housecontrol_invalidate_cache (void) {

    // Reset all remembered triggers. This should be done when a new trigger
    // was declared. The actual trigger used for each control point is
    // a combination that depends on the existence of a default trigger or
    // of a trigger for this control's gear.
    // Which controls are impacted is complex enough that it is more robust
    // to invalidate all cached triggers. A subscribe call is rare anyway.
    int i;
    for (i = 0; i < ControlsCount; ++i) Controls[i].trigger = 0;
}

void housecontrol_subscribe (const char *gear, ControlTrigger trigger) {

    if (!gear) gear = "*";

    unsigned int signature = echttp_hash_signature (gear);

    int i;
    for (i = 0; i < TriggersCount; ++i) {
        if (Triggers[i].signature != signature) continue; // Accelerator.
        if (strcasecmp (gear, Triggers[i].gear)) continue; // No match.

        if (Triggers[i].action == trigger) return; // No change.
        Triggers[i].action = trigger;

        housecontrol_invalidate_cache ();
        return;
    }

    // This is a new type of gear.

    if (TriggersCount >= TriggersSize) {
        TriggersSize += 8;
        Triggers = realloc (Triggers, TriggersSize*sizeof(HouseTrigger));
        if (!Triggers) {
            houselog_trace (HOUSE_FAILURE, gear, "no more memory");
            exit (1);
        }
    }
    i = TriggersCount++;
    Triggers[i].gear = strdup(gear);
    Triggers[i].signature = signature;
    Triggers[i].action = trigger;

    housecontrol_invalidate_cache ();
}

static HouseControl *housecontrol_search (const char *name) {

    int i;
    unsigned int signature = echttp_hash_signature (name);
    for (i = 0; i < ControlsCount; ++i) {
        if (Controls[i].signature != signature) continue; // Accelerator.
        if (!strcmp (name, Controls[i].name)) return Controls + i;
    }

    // This control was never seen before.

    if (ControlsCount >= ControlsSize) {
        ControlsSize += 32;
        Controls = realloc (Controls, ControlsSize*sizeof(HouseControl));
        if (!Controls) {
            houselog_trace (HOUSE_FAILURE, name, "no more memory");
            exit (1);
        }
    }
    i = ControlsCount++;
    Controls[i].name = strdup(name);
    Controls[i].signature = signature;
    Controls[i].gear = 0;
    Controls[i].trigger = 0;
    Controls[i].timestamp = 0;
    Controls[i].state[0] = 0;
    Controls[i].status = 'u';
    Controls[i].deadline = 0;
    Controls[i].uri[0] = 0; // Need to (re)learn.

    return Controls + i;
}

static ControlProvider *housecontrol_search_provider (const char *uri) {

    if (ProvidersAllocated <= 0) return 0;
    int i = echttp_hash_find (&ProvidersCatalog, uri);
    if (i <= 0) return 0;
    return Providers + i;
}

static ParserToken *housecontrol_prepare (int count) {

    static ParserToken *EventTokens = 0;
    static int EventTokensAllocated = 0;

    if (count > EventTokensAllocated) {
        int need = EventTokensAllocated = count + 128;
        EventTokens = realloc (EventTokens, need*sizeof(ParserToken));
    }
    return EventTokens;
}

static void housecontrol_memorize (HouseControl *control,
                                        long long timestamp, const char *state) {
       memccpy (control->state, state, 0, sizeof(control->state));
       control->state[sizeof(control->state)-1] = 0; // Terminator
       control->timestamp = timestamp;
}

static void housecontrol_changes (ControlProvider *provider,
                                       ParserToken *tokens) {

   int *nameslist = 0;
   int *innerlist = 0;

   int startidx = echttp_json_search (tokens, ".start");
   if (startidx < 0) return;
   long long start = tokens[startidx].value.integer;

   int end = echttp_json_search (tokens, ".end");
   if (end < 0) return;
   provider->since = start + tokens[end].value.integer;

   int namesidx = echttp_json_search (tokens, ".names");
   if (namesidx <= 0) return;
   ParserToken *names = tokens + namesidx;

   int nn = tokens[namesidx].length;
   if (nn <= 0) goto cleanup;

   nameslist = calloc (nn, sizeof(int));
   const char *error = echttp_json_enumerate (tokens+namesidx, nameslist, nn);
   if (error) {
       houselog_trace (HOUSE_FAILURE, ".names", "%s", error);
       goto cleanup;
   }

   int changes = echttp_json_search (tokens, ".data");
   if (changes <= 0) return;

   int n = tokens[changes].length;
   if (n <= 0) return;

   innerlist = calloc (n, sizeof(int));
   error = echttp_json_enumerate (tokens+changes, innerlist, n);
   if (error) {
       houselog_trace (HOUSE_FAILURE, ".data", "%s", error);
       goto cleanup;
   }


   int i;
   long long timestamp = start;
   for (i = 0; i < n; ++i) {
       ParserToken *inner = tokens + changes + innerlist[i];
       int event[3];
       error = echttp_json_enumerate (inner, event, 3);
       if (error) {
           houselog_trace (HOUSE_FAILURE, ".data", "item %d: %s", i, error);
           goto cleanup;
       }
       timestamp += inner[event[0]].value.integer;
       const char *name =
           names[nameslist[inner[event[1]].value.integer]].value.string;
       int value = (int) inner[event[2]].value.integer;

       HouseControl *control = housecontrol_search (name);
       if (!control) continue;

       if (control->timestamp >= timestamp) continue; // Ignore repeat.

       const char *new = "off";
       if (value != 0) new = "on";

       const char *old = control->state[0]?control->state:"unknown";
       if (!strcmp (old, new)) {
          // At least one change was missed. The known state is obsolete.
          if (value != 0) old = "off";
          else old = "on";
       }
       DEBUG ("Change for point %s from %s to %s at %lld\n", control->name, old, new, timestamp);
       housecontrol_trigger (control, timestamp, old, new);

       housecontrol_memorize (control, timestamp, new);
   }

cleanup:
   if (innerlist) free (innerlist);
   if (nameslist) free (nameslist);
}

static void housecontrol_update (const char *origin, char *data, int length) {

   ControlProvider *provider = housecontrol_search_provider (origin);
   if (!provider) return;

   provider->replied = time(0);

   int  i;
   char path[256];
   int count = echttp_json_estimate(data);
   ParserToken *tokens = housecontrol_prepare (count);

   const char *error = echttp_json_parse (data, tokens, &count);
   if (error) {
       houselog_trace
           (HOUSE_FAILURE, origin, "JSON syntax error, %s", error);
       return;
   }
   if (count <= 0) {
       houselog_trace (HOUSE_FAILURE, origin, "no data");
       return;
   }

   i = echttp_json_search (tokens, ".control.history");
   if (i >= 0) {
       provider->has_history = 1;
       if (tokens[i].type == PARSER_OBJECT)
           housecontrol_changes (provider, tokens+i);
   }

   i = echttp_json_search (tokens, ".latest");
   if (i >= 0) provider->known = tokens[i].value.integer;

   int controls = echttp_json_search (tokens, ".control.status");
   if (controls <= 0) {
       if (!provider->has_history)
           houselog_trace (HOUSE_FAILURE, origin, "no control data");
       return;
   }

   int n = tokens[controls].length;
   if (n <= 0) {
       houselog_trace (HOUSE_FAILURE, origin, "empty control data");
       return;
   }

   i = echttp_json_search (tokens, ".timestamp");
   if (i < 0) return; // Not normal.
   long long timestamp = tokens[i].value.integer * 1000; // Milliseconds.

   int *innerlist = calloc (n, sizeof(int));
   error = echttp_json_enumerate (tokens+controls, innerlist, n);
   if (error) {
       houselog_trace (HOUSE_FAILURE, path, "%s", error);
       goto cleanup;
   }

   for (i = 0; i < n; ++i) {
       ParserToken *inner = tokens + controls + innerlist[i];
       HouseControl *control = housecontrol_search (inner->key);
       if (strcmp (control->uri, origin)) {
           snprintf (control->uri, sizeof(control->uri), origin);
           control->status = 'i';
           houselog_event_local
               ("CONTROL", control->name, "ROUTE", "TO %s", control->uri);
       }
       int gearidx = echttp_json_search (inner, ".gear");
       if (gearidx >= 0) {
          const char *gear = inner[gearidx].value.string;
          if (!control->gear) {
             control->gear = strdup (gear);
             control->trigger = 0; // force reevaluation later.
          } else if (strcasecmp (gear,control->gear)) {
             free (control->gear);
             control->gear = strdup (gear);
             control->trigger = 0; // force reevaluation later.
          }
       } else if (control->gear) {
          free (control->gear);
          control->gear = 0;
          control->trigger = 0; // force reevaluation later.
       }
       int stateidx = echttp_json_search (inner, ".state");
       if (stateidx > 0) {
           char *state = inner[stateidx].value.string;
           if (strcmp (state, control->state)) {
               const char *old = control->state[0]?control->state:"unknown";
               DEBUG ("Received point %s with state %s (previous: %s)\n", control->name, state, old);
               housecontrol_trigger (control, timestamp, old, state);
               housecontrol_memorize (control, timestamp, state);
           }
       }
   }

cleanup:
   free (innerlist);
}

static int housecontrol_monitor (HouseControl *control, time_t now) {

    if (now > 0) {
        if (control->deadline <= 0) return 0;
        if (control->deadline >= now) return (control->status == 'a');
    }
    // The control timer expired or the control is canceled (now <= 0).
    control->deadline = 0;
    if (control->status == 'a') control->status = 'i';
    return 0;
}

static int housecontrol_rejected (const char *uri, int status) {

   ControlProvider *provider = housecontrol_search_provider (uri);
   if (!provider) return 1;

   if (status == 304) {
       // This is 'nothing changed' (no new data), not an error.
       provider->replied = time(0);
       return 0;
   }

   // We are not sure that the local context is still in sync.
   DEBUG ("Control rejected with status %d for %s\n", status, uri);
   provider->known = provider->since = 0;
   provider->has_history = 0;
   return 1;
}

static void housecontrol_result
               (void *origin, int status, char *data, int length) {

   HouseControl *control = (HouseControl *)origin;

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housecontrol_result, origin);
       return;
   }

   if (status != 200) {
       housecontrol_monitor (control, 0);
       if (!housecontrol_rejected (control->uri, status)) return;

       if (control->status != 'e')
           houselog_trace (HOUSE_FAILURE, control->name, "HTTP code %d", status);
       control->status  = 'e';
       return;
   }
   housecontrol_update (control->uri, data, length);
}

static const char *housecontrol_printable_period (int h, const char *hlabel,
                                                  int l, const char *llabel) {
    static char Printable[128];
    if (l > 0) {
        snprintf (Printable, sizeof(Printable),
                  "FOR %d %s%s, %d %s%s ", h, hlabel, (h>1)?"S":"",
                                      l, llabel, (l>1)?"S":"");
    } else {
        snprintf (Printable, sizeof(Printable),
                  "FOR %d %s%s ", h, hlabel, (h>1)?"S":"");
    }
    return Printable;
}

static const char *housecontrol_printable_duration (int duration) {

    if (duration <= 0) return "";
    if (duration > 86400) {
        duration += 1800; // Rounding.
        return housecontrol_printable_period
                   (duration / 86400, "DAY", (duration % 86400) / 3600, "HOUR");
    } else if (duration > 3600) {
        duration += 30; // Rounding.
        return housecontrol_printable_period (duration / 3600, "HOUR",
                                              (duration % 3600) / 60, "MINUTE");
    } else if (duration > 60) {
        return housecontrol_printable_period (duration / 60, "MINUTE",
                                              duration % 60, "SECOND");
    }
    return housecontrol_printable_period (duration, "SECOND", 0, "");
}

static const char *housecontrol_printable_reason (const char *reason) {

    static char Printable[128];
    if (reason && (*reason > 0)) {
        snprintf (Printable, sizeof(Printable), " (%s)", reason);
    } else {
        Printable[0] = 0;
    }
    return Printable;
}

void housecontrol_sampling (int period) {
    if ((period < 0) || (period >= 1000)) return;
    ControlRequestedSampling = period;
}

int housecontrol_ready (void) {
    return ControlsCount > 0;
}

static const char *housecontrol_cause (const char *text) {
    static char Cause[256];
    if (text) {
        snprintf (Cause, sizeof(Cause), "&cause=");
        int l = strlen(Cause);
        echttp_escape (text, Cause+l, sizeof(Cause)-l);
    } else
        Cause[0] = 0;
    return Cause;
}

const char *housecontrol_state (const char *name) {

    HouseControl *control = housecontrol_search (name);
    if (!control) return 0;
    return control->state;
}

int housecontrol_set (const char *name, const char *state,
                      int pulse, int manual, const char *reason) {

    time_t now = time(0);
    DEBUG ("%lld: Set %s to %s for %d seconds\n",
           (long long)now, name, state, pulse);

    HouseControl *control = housecontrol_search (name);
    if (! control->uri[0]) {
        houselog_event ("CONTROL", name, "UNKNOWN", "");
        return 0;
    }

    if (!reason) reason = "";
    if (manual) {
        houselog_event ("CONTROL", name, state, "%sUSING %s%s",
                        housecontrol_printable_duration (pulse),
                        control->uri,
                        housecontrol_printable_reason (reason));
    }

    char encoded[64];
    echttp_encoding_escape (name, encoded, sizeof(encoded));

    static char url[800];
    snprintf (url, sizeof(url),
              "%s/set?point=%s&state=%s&pulse=%d%s",
              control->uri, encoded, state, pulse,
              housecontrol_cause(reason));
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, name, "cannot create socket for %s, %s", url, error);
        return 0;
    }
    DEBUG ("GET %s\n", url);
    echttp_submit (0, 0, housecontrol_result, (void *)control);
    if (pulse > 0)
        control->deadline = now + pulse;
    control->status = 'a';
    ControlsActive = 1;
    return 1;
}

int housecontrol_start
        (const char *name, int pulse, int manual, const char *reason) {
    return housecontrol_set (name, "on", pulse, manual, reason);
}

static void housecontrol_stop (HouseControl *control, const char *reason) {

    if (! control->uri[0]) return;

    char encoded[64];
    echttp_encoding_escape (control->name, encoded, sizeof(encoded));

    static char url[800];
    snprintf (url, sizeof(url),
              "%s/set?point=%s&state=off%s",
              control->uri, encoded, housecontrol_cause(reason));
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, control->name, "cannot create socket for %s, %s", url, error);
        return;
    }
    DEBUG ("GET %s\n", url);
    echttp_submit (0, 0, housecontrol_result, (void *)control);
    housecontrol_monitor (control, 0);
}

void housecontrol_cancel (const char *name, const char *reason) {

    int i;
    time_t now = time(0);

    if (name) {
        DEBUG ("Trying to cancel point %s\n", name);
        HouseControl *control = housecontrol_search (name);
        if (control->uri[0]) {
            DEBUG ("Canceling point %s\n", name);
            // Do not generate an event if the control was not activated by
            // this service instance: such spurious events are confusing.
            //
            if (control->status == 'a')
                houselog_event ("CONTROL", name, "CANCEL", "USING %s%s",
                                control->uri,
                                housecontrol_printable_reason (reason));
            // Event if the control was not activated by this service instance,
            // still stop it, just to be sure.
            //
            housecontrol_stop (control, reason);
        }
        return;
    }
    DEBUG ("%lld: Cancel all zones and feeds\n", (long long)now);
    for (i = 0; i < ControlsCount; ++i) {
        // This is a broad cancel: as it would be too dangerous to turn off
        // every possible control point, limit the actions to pending (active)
        // actions initiated by this service instance only.
        //
        if (Controls[i].status == 'a') {
            housecontrol_stop ( Controls + i, reason);
        }
    }
    ControlsActive = 0;
}

static void housecontrol_discovered
               (void *origin, int status, char *data, int length) {

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housecontrol_discovered, origin);
       return;
   }

   if (status != 200) {
       if (!housecontrol_rejected ((const char *)origin, status)) return;
       houselog_trace (HOUSE_FAILURE, (const char *)origin,
                       "HTTP error %d", status);
       return;
   }

   housecontrol_update ((const char *)origin, data, length);
}

static void housecontrol_scan_server
                (const char *service, void *context, const char *uri) {

    int i = echttp_hash_insert (&ProvidersCatalog, uri);
    if (i >= ProvidersAllocated) {
        ProvidersAllocated = i + 16;
        Providers = realloc (Providers, ProvidersAllocated*sizeof(ControlProvider));
    }
    // Make sure that the array is initialized if the allocation has holes.
    // (It does have at least one as entry 0 is never used.)
    while (ProvidersCount <= i) {
        Providers[ProvidersCount].uri = 0;
        Providers[ProvidersCount].detected = 0;
        Providers[ProvidersCount].has_history = 0;
        ProvidersCount += 1;
    }
    ControlProvider *provider = Providers + i;
    provider->uri = strdup(uri); // Keep the string.
    provider->detected = time(0);

    char url[256];
    if ((ControlRequestedSampling > 0) && provider->has_history) {
        snprintf (url, sizeof(url), "%s/history?sync=1&since=%lld&period=%d",
                  uri, provider->since, ControlRequestedSampling);
    } else {
        snprintf (url, sizeof(url),
                  "%s/status?known=%lld", uri, provider->known);
    }

    DEBUG ("Attempting poll of provider %d at %s\n", i, url);
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, uri, "%s", error);
        return;
    }
    echttp_submit (0, 0, housecontrol_discovered, (void *)uri);
}

static void housecontrol_discover (time_t now) {

    static time_t latestdiscovery = 0;

    if (!now) { // This is a manual reset (force a discovery refresh)
        latestdiscovery = 0;
        return;
    }

    // If any new control service was detected, force a scan now.
    //
    if ((latestdiscovery > 0) &&
        housediscover_changed ("control", latestdiscovery)) {
        latestdiscovery = 0;
    }

    // Even if nothing new was detected, still scan every seconds, in case
    // the configuration of a service or the state of a control point changed.
    //
    if (now <= latestdiscovery) return;
    latestdiscovery = now;

    // Use the control servers detection time as a flag indicating an active
    // service. Keep trace of all services that were ever known (a service
    // may come back), but those with detected == 0 are considered dead.
    //
    DEBUG ("Reset providers cache\n");
    int i;
    for (i = 0; i < ProvidersCount; ++i) {
        if (Providers[i].replied == 0) {
            DEBUG ("Forget provider %d: %s\n", i, Providers[i].uri);
            Providers[i].known = Providers[i].since = 0;
            Providers[i].has_history = 0;
        }
        Providers[i].detected = 0;
        Providers[i].replied = 0;
    }
    DEBUG ("Proceeding with discovery\n");
    housediscovered ("control", 0, housecontrol_scan_server);
}

void housecontrol_background (time_t now) {

    if (ControlsActive && now) {
        ControlsActive = 0;
        int i;
        for (i = 0; i < ControlsCount; ++i) {
            if (housecontrol_monitor (Controls+i, now))
                ControlsActive = 1;
        }
    }
    housecontrol_discover (now);
}

int housecontrol_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    const char *prefix = "";

    cursor = snprintf (buffer, size, ",\"servers\":[");
    if (cursor >= size) goto overflow;

    for (i = 0; i < ProvidersCount; ++i) {
        if (!Providers[i].detected) continue; // List only active services.
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\"", prefix, Providers[i].uri);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    cursor += snprintf (buffer+cursor, size-cursor, "],\"controls\":[");
    if (cursor >= size) goto overflow;
    prefix = "";

    time_t now = time(0);

    for (i = 0; i < ControlsCount; ++i) {
        if (Controls[i].status != 'a') continue; // List only active controls.
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s[\"%s\",\"%s\",%d]",
                            prefix, Controls[i].name,
                            Controls[i].uri,
                            (int)(Controls[i].deadline - now));
        if (cursor >= size) goto overflow;
        prefix = ",";
    }

    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "STATUS",
                    "BUFFER TOO SMALL (NEED %d bytes)", cursor);
    buffer[0] = 0;
    return 0;
}
