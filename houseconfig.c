/* HouseConfig - A simpler API for reading a program JSON configuration.
 *
 * Copyright 2021, Pascal Martin
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
 * houseconfig.c - Simple API to access a JSON configuration.
 *
 * SYNOPSYS:
 *
 * void houseconfig_default (const char *arg);
 *
 *    Set a hardcoded default for a command line option.
 *
 * typedef void ConfigListener (void);
 * const char *houseconfig_initialize (const char *name, ConfigListener *update,
 *                                     int argc, const char **argv);
 *
 *    Initiate the loading of the configuration based on the specified
 *    command line options below:
 *
 *    -config=STRING         Set the name of the configuration file.
 *                           This also enables local storage.
 *    -use-local-storage     Get the configuration from a local file.
 *    -use-depot-storage     Get the configuration from HouseDepot, not local.
 *    -no-local-storage      Same as above, name deprecated.
 *    -use-local-fallback    Get the configuration from HouseDepot but
 *                           maintain a local configuration file as fallback.
 *
 *    The name parameter represents the name of the application. The name of
 *    the configuration in HouseDepot, or the default name of the configuration
 *    file, will be based on this name, plus a ".json" extension.
 *
 *    The update callback will be called everytime a new configuration has
 *    been loaded. The application can then activate the new configuration.
 *
 * const char *houseconfig_name (void);
 *
 *    Return the basename of the current configuration file.
 *
 * const char *houseconfig_current (void);
 *
 *    Return the JSON data for the current configuration.
 *
 * int houseconfig_active (void);
 *
 *    Return true if a configuration was successfully activated.
 *
 * const char *houseconfig_update (const char *text, const char *reason);
 * const char *houseconfig_save   (const char *text, const char *reason);
 *
 *    Update both the live configuration and the configuration file with
 *    the provided text. The reason parameter is used in events, to identify
 *    what caused the update. That parameter can be null or an empty string.
 *
 *    The houseconfig_update() function activates the new configuration,
 *    while the houseconfig_save() does not. The later is to be used when
 *    the new configuration is already active.
 *
 *    There are two functions because there are two different web API styles
 *    for applying configuration changes:
 *    - The request is a POST that provides a complete new configuration data.
 *      In that case houseconfig_update() should be used.
 *    - The request is a GET that changes one individual item in the
 *      configuration. That item is applied live first, then houseconfig_save()
 *      is called to update the permanent storage (depot service or file).
 *
 * const char *houseconfig_string  (int parent, const char *path);
 * int         houseconfig_integer (int parent, const char *path);
 * int         houseconfig_positive (int parent, const char *path);
 * int         houseconfig_boolean (int parent, const char *path);
 *
 *    Access individual items starting from the specified parent
 *    (the config root is index 0). If the path is an empty string,
 *    the entry being accessed is the parent itself.
 *
 * int houseconfig_array (int parent, const char *path);
 * int houseconfig_array_length (int array);
 *
 *    Retrieve an array.
 * 
 * int houseconfig_enumerate (int parent, int *index, int size);
 *
 *    Retrieve all the elements of an array or object. The index array
 *    must be large enough.
 *
 *    This function returns the actual length of the array, or -1 on error.
 *
 * int houseconfig_object (int parent, const char *path);
 *
 *    Retrieve an object. This returns an index that can be used to retrieve
 *    the object's individual items.
 *
 * void houseconfig_background (time_t now);
 *
 *    Background activity to maintain the state of the configuration.
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housedepositor.h"
#include "houseconfig.h"

// This is the current loaded configuration:
static ParserToken *ConfigParsed = 0;
static int   ConfigTokenAllocated = 0;
static int   ConfigTokenCount = 0;
static char *ConfigText = 0;
static char *ConfigTextCurrent = 0;

#define HOUSECONFIG_PATH "/etc/house/"
#define HOUSECONFIG_EXT  ".json"

static int ConfigFileEnabled = 0;  // Default: no local configuration file.
static int ConfigDepotEnabled = 1; // Default: use HouseDepot.
static int ConfigFallbackEnabled = 0;

static const char *AppName = 0;

static char *ConfigFile = 0;
static char *ConfigName = 0;   // Will point to base name in ConfigFile.
static char *FactoryDefaultsFile = 0;

static time_t ConfigInitialized = 0;

static ConfigListener *ConfigCallback = 0;

static const char *houseconfig_parse (void) {

    if (!ConfigText) {
        ConfigTokenCount = 0;
        return "no configuration";
    }
    int count = echttp_json_estimate(ConfigText);
    if (count > ConfigTokenAllocated) {
        if (ConfigParsed) free (ConfigParsed);
        ConfigTokenAllocated = count;
        ConfigParsed = calloc (ConfigTokenAllocated, sizeof(ParserToken));
    }
    ConfigTokenCount = ConfigTokenAllocated;

    char *proposedconfig = strdup (ConfigText);
    const char *error =
        echttp_json_parse (ConfigText, ConfigParsed, &ConfigTokenCount);
    if (error) {
        free (proposedconfig); // Don't touch the last valid config text.
        ConfigTokenCount = 0;
        houselog_event ("CONFIG", AppName, "ERROR", "%s", error);
        return error;
    }

    // The new proposed config is in service now.
    if (ConfigTextCurrent) free (ConfigTextCurrent);
    ConfigTextCurrent = proposedconfig;
    if (ConfigCallback) ConfigCallback();

    return 0;
}

static void houseconfig_write (const char *text, int length) {

    if ((!ConfigFileEnabled) && (!ConfigFallbackEnabled)) return;

    // TBD: Compare the configuration with the content of the file.
    // If same, don't update. To be done if ConfigFallbackEnabled
    // to avoid write amplification on SD cards (Raspberry Pi).

    int fd = open (ConfigFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd >= 0) {
        write (fd, text, length);
        close (fd);
        houselog_event ("CONFIG", AppName, "SAVED", "TO %s", ConfigFile);
    } else {
        houselog_event ("CONFIG", AppName, "ERROR",
                        "CANNOT WRITE TO %s", ConfigFile);
    }
}

static const char *houseconfig_load_from_file (void) {

    const char *format = "FROM %s";
    const char *name = ConfigFile;

    char *newconfig = echttp_parser_load (ConfigFile);
    if ((!newconfig) && FactoryDefaultsFile) {
        newconfig = echttp_parser_load (FactoryDefaultsFile);
        format = "FROM FACTORY DEFAULT %s";
        name = FactoryDefaultsFile;
    }
    if (!newconfig) {
        houselog_event ("CONFIG", AppName, "ERROR", "NO CONFIGURATION FOUND");
        return "no configuration found";
    }

    // Do not reload the same (valid) configuration again and again.
    if ((ConfigTokenCount > 0) && (!strcmp (newconfig, ConfigTextCurrent)))
        return 0;

    houselog_event ("CONFIG", AppName, "LOAD", format, name);
    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = newconfig;
    return houseconfig_parse ();
}

static void houseconfig_depotlistener (const char *name, time_t timestamp,
                                       const char *data, int length) {

    houselog_event ("CONFIG", AppName, "LOAD", "FROM DEPOT %s", name);
    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = echttp_parser_string (data);

    const char *error = houseconfig_parse();
    if (error) {
        houselog_event ("CONFIG", AppName, "ERROR", "%s", error);
        return;
    }
    houseconfig_write (data, length);
}

void houseconfig_default (const char *arg) {

    const char *name = 0;
    char buffer[1024];

    if (echttp_option_match ("-config=", arg, &name)) {
        if (ConfigFile) free(ConfigFile);
        if ((name[0] == '/') || (name[0] == '.')) {
            ConfigFile = strdup(name);
        } else {
            const char *extension = HOUSECONFIG_EXT;
            if (strchr (name, '.')) extension = "";
            snprintf (buffer, sizeof(buffer),
                      HOUSECONFIG_PATH "%s%s", name, extension);
            ConfigFile = strdup(buffer);
        }
        ConfigFileEnabled = 1;
        ConfigDepotEnabled = 0;
    } else if (echttp_option_present ("-use-local-storage", arg)) {
        ConfigFileEnabled = 1;
        ConfigDepotEnabled = 0;
    } else if (echttp_option_present ("-use-depot-storage", arg)) {
        ConfigFileEnabled = 0;
        ConfigDepotEnabled = 1;
    } else if (echttp_option_present ("-use-local-fallback", arg)) {
        ConfigFileEnabled = 0;
        ConfigFallbackEnabled = 1;
        ConfigDepotEnabled = 1;
    } else if (echttp_option_present ("-no-local-storage", arg)) {
        ConfigFileEnabled = 0;
        ConfigDepotEnabled = 1;
    }
}

const char *houseconfig_initialize (const char *name, ConfigListener *update,
                                    int argc, const char **argv) {

    AppName = strdup (name);

    char buffer[1024];
    snprintf (buffer, sizeof(buffer), "%s%s", name, HOUSECONFIG_EXT);
    ConfigName = strdup(buffer);

    ConfigCallback = update;
    ConfigInitialized = time(0);

    int i;
    for (i = 1; i < argc; ++i) {
        houseconfig_default (argv[i]);
    }

    if (!ConfigFile) {
        // Use the default configuration file name.
        snprintf (buffer, sizeof(buffer), "%s%s", HOUSECONFIG_PATH, ConfigName);
        ConfigFile = strdup (buffer);
    }
    if (!FactoryDefaultsFile) {
        // build the factory default configuration file.
        snprintf (buffer, sizeof(buffer),
                  "/usr/local/share/house/public/%s/defaults.json", AppName);
        FactoryDefaultsFile = strdup (buffer);
    }

    if (ConfigFileEnabled) {
        return houseconfig_load_from_file ();
    }
    if (ConfigDepotEnabled) {
        housedepositor_subscribe
            ("config", ConfigName, houseconfig_depotlistener);
    }
    return 0;
}

const char *houseconfig_update (const char *text, const char *reason) {

    if (houseconfig_active()) {
        if (!strcmp (text, ConfigTextCurrent)) return 0; // No change.
    }

    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = echttp_parser_string (text);
    const char *error = houseconfig_parse ();
    if (error) return error;

    int length = strlen(text);

    if (ConfigDepotEnabled) {
        if (reason && reason[0])
            houselog_event ("CONFIG", AppName, "SAVE",
                            "TO DEPOT %s (%s)", ConfigName, reason);
        else
            houselog_event ("CONFIG", AppName, "SAVE",
                            "TO DEPOT %s", ConfigName);
        housedepositor_put ("config", ConfigName, text, length);
    }

    houseconfig_write (text, length);
    return 0;
}

const char *houseconfig_save (const char *text, const char *reason) {

    ConfigListener *callback = ConfigCallback;
    ConfigCallback = 0; // Temporary disabled, so that there is no activation.
    const char * error = houseconfig_update (text, reason);
    ConfigCallback = callback;
    return error;
}

const char *houseconfig_name (void) {
    return ConfigName;
}

const char *houseconfig_current (void) {
    return ConfigTextCurrent;
}

int houseconfig_active (void) {
    return ConfigTokenCount > 0;
}

int houseconfig_find (int parent, const char *path, int type) {
    int i;
    if (parent < 0 || parent >= ConfigTokenCount) return -1;
    i = echttp_json_search(ConfigParsed+parent, path);
    if (i >= 0 && ConfigParsed[parent+i].type == type) return parent+i;
    return -1;
}

const char *houseconfig_string (int parent, const char *path) {
    int i = houseconfig_find(parent, path, PARSER_STRING);
    return (i >= 0) ? ConfigParsed[i].value.string : 0;
}

int houseconfig_integer (int parent, const char *path) {
    int i = houseconfig_find(parent, path, PARSER_INTEGER);
    return (i >= 0) ? ConfigParsed[i].value.integer : 0;
}

int houseconfig_positive (int parent, const char *path) {
    int i = houseconfig_find(parent, path, PARSER_INTEGER);
    if (i < 0) return 0;
    if (ConfigParsed[i].value.integer < 0) return 0;
    return ConfigParsed[i].value.integer;
}

int houseconfig_boolean (int parent, const char *path) {
    int i = houseconfig_find(parent, path, PARSER_BOOL);
    return (i >= 0) ? ConfigParsed[i].value.boolean : 0;
}

int houseconfig_array (int parent, const char *path) {
    return houseconfig_find(parent, path, PARSER_ARRAY);
}

int houseconfig_array_length (int array) {
    if (array < 0
            || array >= ConfigTokenCount
            || ConfigParsed[array].type != PARSER_ARRAY) return 0;
    return ConfigParsed[array].length;
}

int houseconfig_object (int parent, const char *path) {
    return houseconfig_find(parent, path, PARSER_OBJECT);
}

int houseconfig_enumerate (int parent, int *index, int size) {

    int i, length;

    if (parent < 0 || parent >= ConfigTokenCount) return 0;
    const char *error = echttp_json_enumerate (ConfigParsed+parent, index, size);
    if (error) {
        fprintf (stderr, "Cannot enumerate %s: %s\n",
                 ConfigParsed[parent].key, error);
        return -1;
    }
    length = ConfigParsed[parent].length;
    for (i = 0; i < length; ++i) index[i] += parent;
    return length;
}

void houseconfig_background (time_t now) {

    static time_t LastCall = 0;

    if (now == LastCall) return;
    LastCall = now;

    if (!houseconfig_active ()) {
        if (ConfigDepotEnabled && ConfigFallbackEnabled) {
            if (ConfigInitialized && (now > ConfigInitialized + 120)) {
                // No depot service is responding, or has a configuration.
                // As an act of desperation, use the local config file 
                // (if it exists) as a temporary fallback.
                houseconfig_load_from_file ();
            }
        }
    }

    if (ConfigFileEnabled) {
        // Reload the configuration on a periodic basis: it may have changed.
        // (This does nothing if the file's content is the same as the current
        // configuration.)
        if (now %10 == 0) houseconfig_load_from_file ();
    }
}

