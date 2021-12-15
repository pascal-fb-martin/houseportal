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
 * const char *houseconfig_load (int argc, const char **argv);
 *
 *    Load the configuration from the specified config option, or else
 *    from the default config file.
 *
 * int houseconfig_size (void);
 *
 *    Return the size of the configuration JSON text currently used.
 *
 * const char *houseconfig_update (const char *text);
 *
 *    Update both the live configuration and the configuration file with
 *    the provided text.
 *
 * const char *houseconfig_string  (int parent, const char *path);
 * int         houseconfig_integer (int parent, const char *path);
 * double      houseconfig_boolean (int parent, const char *path);
 *
 *    Access individual items starting from the specified parent
 *    (the config root is index 0).
 *
 * int houseconfig_array (int parent, const char *path);
 * int houseconfig_array_length (int array);
 *
 *    Retrieve an array.
 * 
 * int houseconfig_object       (int parent, const char *path);
 * int houseconfig_array_object (int parent, int index);
 *
 *    Retrieve an object (2nd form: as element of an array).
 * 
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <echttp_json.h>

#include "houseconfig.h"

#define CONFIGMAXSIZE 1024

static ParserToken ConfigParsed[CONFIGMAXSIZE];
static int   ConfigTokenCount = 0;
static char *ConfigText;
static int   ConfigTextSize = 0;
static int   ConfigTextLength = 0;

#define HOUSECONFIG_PATH "/etc/house/"
#define HOUSECONFIG_EXT  ".json"

static const char *ConfigFile = HOUSECONFIG_PATH "portal" HOUSECONFIG_EXT;

static const char *houseconfig_refresh (const char *file) {

    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = echttp_parser_load (file);
    if (!ConfigText) {
        ConfigTextLength = 0;
        ConfigTokenCount = 0;
        return "no configuration";
    }

    ConfigTextLength = strlen(ConfigText);
    ConfigTokenCount = CONFIGMAXSIZE;
    return echttp_json_parse (ConfigText, ConfigParsed, &ConfigTokenCount);
}

void houseconfig_default (const char *arg) {

    const char *name = 0;

    if (strncmp ("--config=", arg, 9) == 0) {
        name = arg + 9;
    } else if (strncmp ("-config=", arg, 8) == 0) {
        name = arg + 8;
    } else {
        return;
    }

    if (name[0] == '/') {
        ConfigFile = strdup(name);
    } else {
        char buffer[1024];
        const char *extension = HOUSECONFIG_EXT;
        if (strchr (name, '.')) extension = "";
        snprintf (buffer, sizeof(buffer),
                  HOUSECONFIG_PATH "%s%s", name, extension);
        ConfigFile = strdup(buffer);
    }
}

const char *houseconfig_load (int argc, const char **argv) {

    int i;

    for (i = 1; i < argc; ++i) {
        houseconfig_default (argv[i]);
    }
    return houseconfig_refresh (ConfigFile);
}

const char *houseconfig_update (const char *text) {

    int fd;

    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = echttp_parser_string (text);
    if (!ConfigText) {
        ConfigTextLength = 0;
        ConfigTokenCount = 0;
        return "no configuration";
    }
    ConfigTextLength = strlen(ConfigText);
    ConfigTokenCount = CONFIGMAXSIZE;
    const char *error =
        echttp_json_parse (ConfigText, ConfigParsed, &ConfigTokenCount);
    if (error) {
        echttp_parser_free(ConfigText);
        ConfigText = 0;
        ConfigTextLength = 0;
        ConfigTokenCount = 0;
        return error;
    }
    fd = open (ConfigFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd >= 0) {
        write (fd, text, ConfigTextLength);
        close (fd);
    }
    return 0;
}

int houseconfig_size (void) {
    return ConfigTextLength;
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

int houseconfig_boolean (int parent, const char *path) {
    int i = houseconfig_find(parent, path, PARSER_BOOL);
    return (i >= 0) ? ConfigParsed[i].value.bool : 0;
}

int houseconfig_array (int parent, const char *path) {
    return houseconfig_find(parent, path, PARSER_ARRAY);
}

int houseconfig_array_object (int parent, int index) {
    char path[32];
    snprintf (path, sizeof(path), "[%d]", index);
    return houseconfig_find(parent, path, PARSER_OBJECT);
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

