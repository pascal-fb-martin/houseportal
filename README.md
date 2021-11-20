# HousePortal

## Overview

This is a web server to redirect HTTP requests to multiple specialized web servers. This project depends on [echttp](https://github.com/pascal-fb-martin/echttp).

Having multiple specialized web servers run on the same machine, typically web servers embedded in applications, causes a port conflict situation: which application will claim port 80?

This software is intended to resolve that specific issue by managing redirects in an automatic fashion:
* A machine runs HousePortal as port 80, and applications A, B, C, etc. on different ports (including dynamic port numbers).
* Each application periodically sends a UDP packet to the portal to register their redirection (by providing their web access port number and root path).
* When the portal receives a request that match the root path of a registered redirection, it replies with a 302 Found redirect indicating the full URI to use.

A static configuration files allows HousePortal to be compatible with existing web applications not designed to support this scheme.

This makes HousePortal a discovery service that is compatible with the HTTP protocol, web servers and web browsers.

HousePortal may register targets from remote machines, but this is not the main goal. HousePortal was intended as a single endpoint from which multiple independent local service can be accessed, hiding the local machine configuration and presenting a single web interface to the outside.

## Installation

* Install the openssl development package(s)
* install [echttp](https://github.com/pascal-fb-martin/echttp).
* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/house/portal.config

## Configuration File

The default HousePortal configuration is /etc/house/portal.config. A different configuration file can be specified using the -config=path option. The configuration file is a list of directives, one directive per line. Each directive starts with a keyword, with a variable count of space-separated arguments. Lines starting with character '#' are comments and ignored.

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

      'REDIRECT' time [host:]port [HIDE] [[service:]path ..] [SHA-256 signature]
      
where host is a host name or IP address, time is the system time when the message was formatted (see time(2)), port is a number in the range 1..65535 and each path item is an URI's absolute path (which must start with '/'), optionally prefixed with a service name (see the service section later).

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

A redirection may define the target as a service. HousePortal maintains a list of active targets for each service name. That list can be queried by outside clients that need to discover which URL to use for these services.

A service is a generic name that uniquely identify the web API supported by the target. For example a service may define how to access sensors, external controls (relays, triac, etc), or how to control of a sprinkler system. A target may be an implementation of the service for a certain type of resource, for example the control service may be associated with two targets, one implementing an interface to a relay boards, and the other using the OpenSprinkler Triac interface.

This service discovery is not concerned with parallelism or clustering: the client needs to query each target to discover more details about the service.

The intent of the HousePortal service discovery is to provide a single endpoint on each server from which services hosted by this server can be discovered. For example a client would query all known servers to find on which servers reside the services it need to access. HousePortal is not intended to act as a global service discovery service, i.e. a single endpoint that will consolidate all the service configuration information for a complete network.

The list of server to query can itself be discovered by sending a request to the local HousePortal server (See the description of the PEER message in a previous section). Thus any discovery takes two phase:
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

First the application must include the client header file:
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
The next step is to register the various paths:
```
void houseportal_register (int webport, const char **paths, int count);
void houseportal_register_more (int webport, const char **paths, int count);
```
Once all this was done, the application must periodically renew its paths:
```
void houseportal_renew (void);
```

### Discovery client API

This API can used by a web client to automatically find which services are running. It is up to the web client to find which one of the services found to use.

The HousePortal web API for discovery can always be used raw. This API hides the complete discovery sequence, performs the discovery in an asynchronous mode and caches the result.

First the application must include the client header file:
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

A House service typically keeps two logs: traces (for maintainer) and events (for users). This history is separate for each application.

All logs are stored in daily files (see later for more details).

The 256 latest events are also kept in a memory buffer. If the /{app}/log/events URI is used with no parameter, all events present in memory are returned.

The following API is used to record events and traces in the application:

```
void houselog_initialize (const char *application,
                          int argc, const char **argv);
```
This initializes the memory storage for the recent logs and register all the URI paths with echttp. Note that the log module declares its own WEB API: the application does not need to declare routes on its own. The application name is used to build the URI paths, and the name of the event files.

It is possible to force a different history storage path with the "-log=PATH" command line option. This is however not recommended.

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
void houselog_trace (HOUSE_INFO or HOUSE_WARNING or HOUSE_FAILURE,
                     const char *object,
                     const char *format, ...);
```
The three macros above actually hide the file name, line number and level parameters. The object parameter can be used as a filtering criteria when going through the logs, and the application is free to use any name it wants; it is recommended to populate it with an ID of the resource that the trace is related to. The other parameters are used to generate a free format text.

```
void houselog_background (time_t now);
```
This function must be called at regular intervals for background processing, such as cleanup of expired resources, saving data to permanent storage, etc.

## Log File Management

All logs are kept in daily files (CSV format) under /var/lib/house/log. That directory is the root of a tree organized in three levels: year (4 digits), month (2 digits) and day of the month (2 digits). The event log file is named {app}_e_{year}{month}{day}.csv and the trace file is named {app}_t_{year}{month}{day}.csv.

(The purpose of the directory tree structure is to avoid having hundreds of files in a single directory, which may slow down file operations. The date is repeated in the file name because of web download, which strips the path: doing it this way prevents file for different days to overwrite each other.)

A web access is provided:
* Events can be accessed through the /{app}/log/events URI,
* Traces can be accessed through the /{app}/log/traces URI, and
* A log file can be downloaded using the /log/files/{year}/{month}/{day}/{app}_[t|e]_{year}{month}{day}.csv URI.

The /{app}/log/events and /{app}/log/traces URI accept two optional parameters:
* date: date of the first record shown, format is YYYY-MM-DD
* time: time of the first record shown, format is HH:MM

In order to avoid writing frequently to SD cards, the active logs are written to /dev/shm, and moved to permanent storage at the end of the day. To limit loss of data on power outage, the logs are also saved to permanent storage every hour.

During initialization, the log module tries to backup any existing log file in /dev/shm and then restore any existing current day file from permanent storage to /dev/shm.

Any of these file operations is performed only if the source is more recent than the destination.

## Docker

The project supports a Docker container build, which was tested on an ARM board running Debian. To make it work, all the house containers should be run in host network mode (`--network` host option). This is because of the way [houseportal](https://github.com/pascal-fb-martin/houseportal) manages access to each service: using dynamically assigned ports does not mesh well with Docker's port mapping.

