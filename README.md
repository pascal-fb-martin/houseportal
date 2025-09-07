# HousePortal

## Overview

This is a web server to redirect HTTP requests to multiple specialized web servers. This project depends on [echttp](https://github.com/pascal-fb-martin/echttp).

See the [gallery](https://github.com/pascal-fb-martin/houseportal/blob/master/gallery/README.md) for a view of HousePortal's web UI.

Having multiple specialized web servers run on the same machine, typically web servers embedded in applications, causes a port conflict situation: which application will claim port 80?

This software is intended to resolve that specific issue by managing redirects in an automatic fashion:

* A machine runs HousePortal as port 80, and applications A, B, C, etc. on different ports (including dynamic port numbers).
* Each application periodically sends a UDP packet to the portal to register their redirection (by providing their web access port number and root path).
* When the portal receives a request that match the root path of a registered redirection, it replies with a 302 Found redirect indicating the full URI to use.

A static configuration files allows HousePortal to be compatible with existing web applications not designed to support this scheme.

This makes HousePortal a discovery service that is compatible with the HTTP protocol, web servers and web browsers.

HousePortal may register targets from remote machines, but this is not the main goal. HousePortal was intended as a single endpoint from which multiple independent local service can be accessed, hiding the local machine configuration and presenting a single web interface to the outside.

## Installation

* Install the openssl development package(s) and icoutils.
* install [echttp](https://github.com/pascal-fb-martin/echttp).
* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/house/portal.config

## Configuration File

The default HousePortal configuration is /etc/house/portal.config. A different configuration file can be specified using the -config=PATH option. The configuration file is a list of directives, one directive per line. Each directive starts with a keyword, with a variable count of space-separated arguments. Lines starting with character '#' are comments and ignored.

If the configuration file is modified while HousePortal is running, the current HousePortal configuration will be updated within 30 seconds (except for the LOCAL option, which remains unchanged--see below).

In order to support applications not designed to interact with HousePortal, a static redirection configuration is supported:

      'REDIRECT' [host:]port [HIDE] [[service:]root-path ..]

These static redirections never expire.

The HousePortal servers discover each other within the local subnet, using broadcast. However it is necessary to configure at least one static peer when there are multiple subnets and the broadcast packet will not reach all servers:

      'PEER' host ..

These static declarations never expire.

## Security

A simple form of security is possible by accepting only local UDP packets, i.e. HousePortal to bind its UDP socket to IP address 127.0.0.1. This is typically used when all local applications are trusted, usually because the local machine's access is strictly restricted. That mode is activated when the LOCAL keyword is present in the HousePortal configuration at the time HousePortal starts:

       'LOCAL'

To support security in an open access network, the use of cryptographic signatures may be required by specifying cryptographic keys:

       'SIGN' 'SHA-256' key

Where the key is an hexadecimal string (64 bytes) that must be used by clients when computing their signature. The SIGN keyword may be used multiple times: HousePortal will try to use each key matching the cypher used by the client until the source has been authenticated successfully. If no match was found, for any reason, the packet is ignored. It is valid to declare a key for an unknown cypher, but it will never get used.

It is valid to combine both the local mode and cryptographic authentication. This is typically used if multiple users have access to the host and the outside network is not trusted at all.

If no cryptographic key is provided, HousePortal will accept all redirection messages, with or without signature. If at least one cryptographic key is provided, HousePortal will require every redirection message to be signed: if no signature matches, or if no key is applicable to the provided root path, the redirection message is ignored.

## Protocol.

UDP port 70 is used for redirection registrations, because this port is assigned to the Gopher protocol and, let's be serious, who use Gopher nowadays?

A redirection message is a space-separated text that follows the syntax below:

```
'REDIRECT' time [host:]port [HIDE] [PID:pid] [[service:]path ..] [SHA-256 signature]
```

where host is a host name or IP address, time is the system time when the message was formatted (see time(2)), port is a number in the range 1..65535 and each path item is an URI's absolute path (which must start with '/'), optionally prefixed with a service name (see the service section later). The optional PID argument can be used to monitor the process. It is also used to detect service restart (i.e. when the PID changes).

The "/portal" path name is reserved for HousePortal's own status.

If the host is missing, HousePortal uses the host name of the local machine.

The HIDE option is meant to simplify redirection rules when the original web server's URLs do not have an identifiable root. It allows the portal to rely on a URL prefix to select the redirection, but not convey that prefix to the target. For example the redirection message:

```
      REDIRECT 12345678 8080 HIDE /app
```

causes the HTTP request

```
      http://myserver/app/complex/application/path
```

to be redirected as:

```
      http://myserver:8080/complex/application/path
```

An optional cryptographic signature can be used to authenticate the source of the redirection. That signature is calculated over the text of the redirection message, excluding the signature portion. The signature is defined as a SHA-256 HMAC.

HousePortal will redirect to the specified port any request which absolute path starts with the specified root path. There is no response to the redirect message.

The registration must be periodic:

* This allows HousePortal to detect applications that are no longer active.
* This allows redirections to recover from a HousePortal restart.

All HousePortal servers talk to each other through the PEER message, which follows the syntax below:

      'PEER' time host host[=expiration] .. [SHA-256 signature]

Each instance reports all the peer hosts it knows about, listing itself first. The message is sent as a broadcast, and as a unicast to each statically declared peer. This makes it possible to do discovery even through a router:

* Declare a static peer (or two for redundancy) across the router, on both sides of the router.
* The static peers declared will receive the unicast message and discover the other peers known to this instance.

The expiration time must be specified when the peer host was not statically configured: this is so that the actual expiration time shared by all instances match an actual detection of this host, not just a second hand report.

Hosts that are statically configured on an instance will be maintained as live as long as this instance is live, even if these hosts are not actually live themselves.

## Service Discovery

HousePortal maintains a list of active targets for each service name. That list can be queried by outside clients that need to discover which URL to use for these services.

A service is a generic name that uniquely identifies the web API supported by the target. For example a service may define how to access sensors, external controls (relays, triac, etc), or how to control of a sprinkler system. A target may be an implementation of the service for a certain type of resource, for example the control service may be associated with two targets, one implementing an interface to a relay boards, and the other using the OpenSprinkler Triac interface.

This service discovery is not concerned with parallelism or clustering: the client needs to query each target to discover more details about the service.

The intent of the HousePortal service discovery is to provide a single endpoint on each server from which services hosted by this server can be discovered. For example a client would query all known servers to find on which servers reside the services it need to access. HousePortal is not intended to act as a global service discovery service, i.e. there is no single endpoint that will consolidate all the service configuration information for a complete network. An application must query each server independently.

The list of servers to query can itself be discovered by sending a request to the local HousePortal server (See the description of the PEER message in a previous section). Thus any discovery takes two phases:

* The first phase is to request the list of known HousePortal servers to the local one:

```
GET /portal/peers
```

The response is a JSON object with the list of known HousePortal server names:

```
{
    portal : {
        host : "...",
        timestamp : ...,
        peers : ["...", ..]
    }
}
```

* The second phase is to request the list of known services to each of the listed HousePortal servers:

```
GET /portal/service?name=...
```

The name parameter is mandatory. The response is a JSON object as shown below:

```
{
    portal : {
        host : "...",
        timestamp : ...,
        service : {
            name : "...",
            url : ["...", ..]
        }
    }
}
```

The url item is a list of root URL for the service's endpoints. HousePortal will typically point each URL to itself, with the proper path associated with the target. This way the client may not need to refresh the service's URL list as often as if the URL strings were denoting the actual targets.

## House Library API

The HousePortal library includes a set of generic modules that are shared among all applications in the House suite of services. This library reduces the effort required to write a new application, and provides consistency among all House applications.

### Portal Client API

A web server can be coded to advertize its port number to HousePortal using the HousePortal client API.

The application must include the client header file:

```
#include "houseportalclient.h"
```

The application must then initialize the client interface:

```
void houseportal_initialize (int argc, const char **argv);
```

The houseportal_initialize function decodes the following command line options:

* --portal-port=N defines a non-default port for the HousePortal UDP interface.
* --portal-server=NAME to use a HousePortal server on a different machine.
* --portal-map=NN:NN declares the port mapping used by a proxy or firewall.

(The port mapping option can be repeated for each port used by the application.  HousePortal does not support any load balancing: the same service cannot be declared by more than one application to a single HousePortal server.)

If any cryptographic signature is required, the key must be provided:

```
void houseportal_signature (const char *cypher, const char *key);
```

The next step is to declare the various URI paths:

```
void houseportal_declare (int webport, const char **paths, int count);
void houseportal_declare_more (int webport, const char **paths, int count);
```

Once all this was done, the application must periodically call the background function, which renews the registered paths:

```
void houseportal_background (time_t now);
```

The following API functions are still supported but are deprecated:

```
void houseportal_register (int webport, const char **paths, int count);
void houseportal_register_more (int webport, const char **paths, int count);
void houseportal_renew (void);
```

(The newer API is simpler to use and the period of the renewals is defined in a single place.)

### Discovery client API

This API can used by a web client to automatically find which services are running. It is up to the web client to find which one of the services found to use.

The HousePortal web API for discovery can always be used raw. This API hides the complete discovery sequence, performs the discovery in an asynchronous mode and caches the result.

The application must include the client header file:

```
#include "housediscover.h"
```

The application must then initialize the client interface:

```
void housediscover_initialize (int argc, const char **argv);
```

The application must then proceed with the background discovery, either periodically or, whenever possible, at least 10 seconds before the result is needed:

```
void housediscover (const char *service);
```

This function must be called for each service that the application needs. It is asynchronous: the result of the discovery wil be available later, when the HousePortal servers responses have been received.

The application may find if a new service has been detected by periodically walk the local discovery cache:

```
int housediscover_changed (const char *service, time_t since);
```

This returns true if any new service was detected since the specified time.

The application gets the result of the discovery by walking through the local discovery cache:

```
typedef void housediscover_consumer
                 (const char *service, void *context, const char *url);

void housediscovered (const char *service, void *context,
                      housediscover_consumer *consumer);
```

Note that there is no indication of when  the discovery is complete, since some HousePortal may never answer. No matter the pending discovery status, the local cache always contains the latest up-to-date information, but this result might be incomplete if a discovery is pending.

Because the discovery mechanism involves multiple HTTP queries, it is recommended not to proceed with the discovery too frequently.

### Log API

The log API is used to record what is happening inside the application and to save these records to permanent storage. It also implements a web API used to update web pages that display the log data.

A House service can provide five different types of logs:

- Events (for users) are saved to permanent storage and are accessible through web UIs.
- Traces (for software maintainers) are saved to permanent storage.
- Metrics (for system administrators) are saved to permanent storage (at a reduced rate) and are accessible through web UIs as graphs.
- Sensor data (for later analysis) is saved to permanent storage and is accessible through a limited web UI (mostly for troubleshooting).
- Captures (for on site troubleshooting) are accessible through a web UI but are only active on request.

Each application (service) generates its own logs. Except for captures, these logs are consolidated through an historian service: see [HouseSaga](https://github.com/pascal-fb-martin/housesaga).

Deciding when to generate an event, trace, sensor, metric or capture record is not always obvious. Following the basic rules below may help:

- If the message is comprehensible and useful to a mainstream user without software experience, then it should probably be an event.
- If the message indicates a problem that a mainstream user can resolve without modifying the software, then it should be an event.
- If interpreting the message requires some knowledge of the source code, then it should be a trace.
- If the message indicates a problem that could be resolved only by modifying the software, then it should be a trace.
- Traces should not record normal and usual events. If the volume of traces becomes large, either the advertised issue should be solved or else the source code should be modified to limit the amount of traces produced.
- Avoid events and trace duplicates (i.e. report the same fact as both), except if it can be used to match the two logs during analysis.
- Metrics are to record resources usage or performance of the infrastructure.
- If the volume of messages is very large, then it probably should be a capture.
- If the message records raw data coming in or generated by the application, then it should be a capture.
- If the message records an internal script call, then it should be a capture.
- The sensor log is strictly to record the value of an external sensor. Events can be generated if the sensor's value is outside of a "normal" range.

#### Event and trace API

The 256 latest events are kept in a memory buffer. The /{app}/log/events URI returns all events current stored in memory.

A service generating events automatically detects all running history services:

* If no history services are running, events and traces will not be stored to files (events can still be viewed from the service's own web UI).

* If multiple history services are running, events and traces will be duplicated across all history services present: this can be used as a redundancy feature.

The benefits of using a centralized history service are:

* Events and traces from all services are consolidated in one single place, on one system.
* This avoids causing a high write activity on a Raspberry Pi MicroSD card, which could impact its lifetime. The history service is meant to run on a file server.

The log API depends on the service discovery mechanism: the application must call the discovery client API.

The application must include the client header file:

```
#include "houselog.h"
```

```
void houselog_initialize (const char *application,
                          int argc, const char **argv);
```

This initializes the memory storage for the recent logs and register all the URI paths with echttp. Note that the log module declares its own WEB API: the application does not need to declare routes on its own. The application name is used to build the URI paths, and the name of the event files.

It is possible to force a different history storage path with the "-log=PATH" command line option. This is however not recommended.

This function consumes the same "--portal-host=NAME" option as houseportal_initialize.

```
void houselog_event (const char *category,
                     const char *object,
                     const char *action,
                     const char *format, ...);
```

Record one more event. The event is added to the in-memory list of recent event, potentially removing the oldest event, and is stored to the event history file for the hour that matches the provided timestamp.

* The category and object parameters describe what device or resource this event is related to; by convention category describes a type of device or resource, and object provides a user-friendly identifier of the resource.
* The action parameter indicates what happened to the resource, typically a verb or a state; for some input devices, such as analog sensors, the action parameter typically represents the value of the input.
* The format and subsequent parameters are used to build a free format text providing additional information specific to the category of the device. See the printf(3) man page for a description of the formatting tags.

These parameters are saved as is to the CSV history file.

```
void houselog_event_local (const char *category,
                           const char *object,
                           const char *action,
                           const char *format, ...);
```

This function is similar to houselog_event(), with the exception that this event
will _not_ be saved to files. This is typically used for verbose minor events, which value is only for immediate diagnostics.

```
void houselog_trace (HOUSE_INFO or HOUSE_WARNING or HOUSE_FAILURE,
                     const char *object,
                     const char *format, ...);
```

The three macros above actually hide the file name, line number and level parameters. The object parameter can be used as a filtering criteria when going through the logs, and the application is free to use any name it wants; it is recommended to populate it with an ID of the resource that the trace is related to. The other parameters are used to generate a free format text.

```
void houselog_background (time_t now);
```

This function must be called at regular intervals for background processing, such as cleanup of expired resources, saving data to permanent storage, etc.

```
const char *houselog_host (void);
```

This function returns the name of the local machine, as used in the logs.

#### Sensor API

The application must include the client header file:

```
#include "houselog_sensor.h"
```

```
void houselog_sensor_initialize (const char *application,
                                 int argc, const char **argv);
```

This initializes the environment required to record sensor data. This must be the first sensor API function that the application calls.

```
void houselog_sensor_data (const struct timeval *timestamp,
                           const char *location, const char *name,
                           const char *value, const char *unit);
```

Submit a new sensor data record. The timestamp parameter is used as a one millisecond precision time (microseconds are ignored).

```
void houselog_sensor_numeric (const struct timeval *timestamp,
                              const char *location, const char *name,
                              long long value, const char *unit);
```

A variant of houselog_sensor_data() to use when the sensor value is numeric (integer or fixed point--e.g. milli amperes).

```
void houselog_sensor_flush (void);
```

Force transmission of pending sensor data. The data is buffered until the buffer is full, or the flush is requested. This mechanism is intended for applications that receive sensor data in packets and know when it is appropriate to flush.

```
void houselog_sensor_background (time_t now);
```

This function must be called a regular intervals for background processing.

#### Capture API

The capture log feature is separate from the other logs because it is not subject to consolidation and storage: there is no log file.

The application must include the client header file:

```
#include "housecapture.h"
```

```
void housecapture_initialize (const char *root,
                              int argc, const char **argv);
```

This initializes the environment required to capture data. This must be the first capture API function that the application calls.

```
int housecapture_register (const char *category);
```

Register a new category of capture data. Categories are used for quick and low overhead filtering in the capture module. Registration should occur once per category, during the application's initialization. This returns the index of the category, which is used in subsequent calls.

```
int housecapture_registered (void);
```

These functions are meant to provide a way to propagate the state of capture to other parts of the application, e.g. to other threads. The capture module should only be used in the one thread that runs the web server.

```
time_t housecapture_active (int category);
```

Return the current capture start time if capture is active for the specified category, 0 otherwise.

A caller will typically use the value returned as a boolean, to avoid significant processing overhead that could be required to gather information for the capture record. Note that housecapture_record() performs the same checks, so this is just meant as an optimization.

The actual start time can be used to implement some timeout guardrail, i.e. to avoid capturing data forever in some other thread.

```
void housecapture_record (int category,
                          const char *object,
                          const char *action,
                          const char *format, ...);
```

Record new capture data. All capture data is local: there is no intent to consolidate and archive capture data. Use events for significant information worth archiving.

Nothing is recorded if the capture is not active for the specified category, or if there is a filter condition that the data does not meet.

The object identifies the resource related to the recorded data, for example a netowrk address, a sensor name, etc.

An action represents provides some context for the data, for example "RECEIVE", "SEND", "TRIGGER", "IGNORE", etc. An action will typically not appear in the data itself. The action also provides an additional filter criteria.

```
void housecapture_background (time_t now);
```

This function must be called a regular intervals for background processing.

### Configuration API

This module provides a simplified API to handle configuration files in JSON format. This API is easier to use than the general purpose echttp JSON API, but it assumes that there is only one JSON context, for a single file which name is specified by the application.

The application must include the client header file:

```
#include "houseconfig.h"
```

```
void houseconfig_default (const char *arg);
```

This function allows the application to define default options. The options consumed by the configuration API are:

* --config=NAME: the name (and path) of the configuration file.
* --no-local-storage: ignore local configuration files. A depot service must be running when this option is used.

The name provided with the --config option is either a full path (starting with '/') or a path relative to `/etc/house`. If the path is relative, the file extension may also be omitted, in which case ".json" is used. All three examples below are equivalent:

```
houseconfig_default ("--config=/etc/house/app.json");
houseconfig_default ("--config=app.json");
houseconfig_default ("--config=app");
```

An application must call houseconfig_default() so that the default name of the configuration file matches the name of the application. It is recommended to always use the shortest form (see third example above), to keep the default file path consistent between applications, and defined only once. However an application may use an alternate scheme if needed.

```
const char *houseconfig_load (int argc, const char **argv);
```

This function loads the existing configuration. This is called on application startup. The argc and argv parameters should represent the command line arguments. See houseconfig_default() for a description of the command line options supported.

```
int houseconfig_active (void);
```

This function returns true if a configuration was successfully activated.

```
const char *houseconfig_current (void);
```

This function returns the JSON text matching the most recent successfully activated configuration. Invalid configurations activated since then are ignored.

```
const char *houseconfig_name (void);
```

This function returns the base name of the configuration. This is typically
used when interfacing with the HomeDepot service.

```
const char *houseconfig_update (const char *text);
```

This function provides a replacement configuration. This is typically used after the user edited and posted a new configuration. The module first proceed with the decoding of the JSON data. If successful, the string is saved to the configuration file. On return the application can then start accessing and applying the new configuration.

```
int houseconfig_find (int parent, const char *path, int type);
```

This function searches for the specified path, starting at the specified index of the parent object. Index 0 is the whole JSON data structure.

```
const char *houseconfig_string (int parent, const char *path);
int         houseconfig_integer (int parent, const char *path);
int         houseconfig_boolean (int parent, const char *path);
```

These functions return the value of the specified item.

```
int houseconfig_array        (int parent, const char *path);
int houseconfig_array_length (int array);
```

The first function returns the index to the specified array, while the second function returns the number of item (length) of the array referenced by its index.

```
int houseconfig_object (int parent, const char *path);
int houseconfig_array_object (int parent, int index);
```

These functions return the index to a specific object. The first form return a sub-object of the specified parent. The second form returns the Nth object in an array of objects.

### Depot Client API (Depositor)

The depot service stores configuration and state files for other services. This approach provides the following benefits:

* If copies of the same service run on multiple machines (for redundancy purpose), they share the same configuration. Configuration changes are automatically propagated.
* The depot service manages an history, allowing to undo recent changes: see [HouseDepot](https://github.com/pascal-fb-martin/housedepot)
* Running the depot service on multiple servers provides a redundancy feature.

This API comes as a complement to the configuration API, and is designed to work in combination.

The depot service mechanism is in addition to, and overwrites, the local configuration file. The local configuration is loaded first. Whenever a depot service has been detected that provides the same configuration file, the depot version is loaded to replace the local one. Any configuration change, either made locally or detected from a depot service, is stored locally. The local configuration is thus always up-to-date, allowing the application to function even if no depot service is accessible.

The application must include the client header file:

```
#include "housedepositor.h"
```

```
void housedepositor_default (const char *arg);
```

This function sets default values for command line options. Call the function once for each options to set a default for. This function must be called before housedepositor_initialize().

```
void housedepositor_initialize (int argc, const char **argv);
```

This function initializes the client context. One configuration parameter is consumed: `--group=STRING` (hardcoded default is 'home'). The group name is used to distinguish between multiple instances of the same service that would use separate configurations.

Some services are hardware-dependent, i.e. the configuration is specific to the machine that the service runs on. In this case, the service should set a default group based on the name of the machine it runs on.

```
typedef void housedepositor_listener (const char *name, time_t timestamp,
                                      const char *data, int length);

void housedepositor_subscribe (const char *repository,
                               const char *name,
                               housedepositor_listener *listener);
```

This function declares an application listener, which will be called whenever a new revision of the file identified by `repository` and `name` is detected.

The depositor client supports listening on multiple files. Using multiple configuration files is typically done when the overall configuration is split into separate sets, such as a user-entered configuration ('config' repository), or application-generated state to be restored on restart ('state' repository).

```
int housedepositor_put (const char *repository,
                        const char *name,
                        const char *data, int size);
```

This function submits a new revision of the specified configuration file to all depot services currently detected. If other services listen to the same file, they will all be notified of the new revision.

```
void housedepositor_periodic (time_t now);
```

This background function must be called periodically. It handles the discovery of, and queries to, the depot services.

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

```
make debian-package
```

## Docker

The project supports a Docker container build, which was tested on an ARM board running Debian. To make it work, all the house containers should be run in host network mode (`--network host` option). This is because of the way [houseportal](https://github.com/pascal-fb-martin/houseportal) manages access to each service: using dynamically assigned ports does not mesh well with Docker's port mapping.

