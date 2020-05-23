# HousePortal

# Overview

This is a web server to redirect HTTP requests to multiple specialized web servers. This project depends on [echttp](https://github.com/pascal-fb-martin/echttp).

Having multiple specialized web servers run on the same machine, typically web servers embedded in applications, causes a port conflict situation: which application will claim port 80?

This software is intended to resolve that specific issue by managing redirects in an automatic fashion:
* A machine runs HousePortal as port 80, and applications A, B, C, etc. on different ports (including dynamic port numbers).
* Each application periodically sends a UDP packet to the portal to register their redirection (by providing their web access port number and root path).
* When the portal receives a request that match the root path of a registered redirection, it replies with a 302 Found redirect indicating the full URI to use.

# Installation

* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/houseportal/houseportal.config
* Restart houseportal.

# Protocol.

UDP port 70 is used for redirection registrations, because this port is assigned to the Gopher protocol and, let's be serious, who use Gopher nowadays?

A redirection message is a space-separated text that follows the syntax below:

      'REDIRECT' time [host:]port [HIDE] [path ..] [SHA-256 signature]
      
where host is a host name or IP address, time is the system time when the message was formatted (see time(2)), port is a number in the range 1..65535 and each path item is an URI's absolute path (which must start with '/').

The "/portal" path name is reserved for houseportal's own status.

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

# Configuration File

The default HousePortal configuration is /etc/houseportal/houseportal.config. A different configuration file can be specified using the -config=path option. The configuration file is a list of directives, one directive per line. Each directive starts with a keyword, with a variable count of space-separated arguments.

In order to support applications not designed to interact with HousePortal, a static redirection configuration is supported:

      'REDIRECT' [host:]port [HIDE] [root-path ..]

These static redirections never expire.

# Security

A simple form of security is possible by accepting only local UDP packets, i.e. HousePortal to bind its UDP socket to IP address 127.0.0.1. This is typically used when all local applications are trusted, usually because the local machine's access is strictly restricted. That mode is activated when the LOCAL keyword is present in the HousePortal configuration:

       'LOCAL'

To support security in an open access network, the use of cryptographic signatures may be required by specifying cryptographic keys:

       'SIGN' 'SHA-256' key

Where the key is an hexadecimal string (64 bytes) that must be used by clients when computing their signature. The SIGN keyword may be used multiple times: houseportal will try to use each key matching the cypher used by the client until the source has been authenticated successfully. If no match was found, for any reason, the packet is ignored. It is valid to declare a key for an unknown cypher, but it will never get used.

It is valid to combine both the local mode and cryptographic authentication. This is typically used if multiple users have access to the host and the outside network is not trusted at all.

If no cryptographic key is provided, HousePortal will accept all redirection messages, with or without signature. If at least one cryptographic key is provided, HousePortal will require every redirection message to be signed: if no signature matches, or if no key is applicable to the provided root path, the redirection message is ignored.

# Client API

A web server can be coded to advertize its port number to houseportal using the houseportal client API.

First the application must include the client header file:
```
#include "houseportalclient.h"
```
The client must then initialize the client interface:
```
void houseportal_initialize (int argc, const char **argv);
```
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

