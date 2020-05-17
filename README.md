# houseportal
A web server to redirect to multiple specialized web servers.

When you run multiple specialized web servers on the same machine, typically web servers embedded in applications, you get into a conflicting situation: who claims port 80?

This software is intended to resolve that specific issue by managing redirects in an automatic fashion:
* A machine runs houseportal as port 80, and applications A, B, C, etc. on different ports (including dynamic port numbers).
* Each application periodically sends a UDP packet to the portal to register their redirection (by providing their web access port number and root path).
* When the portal receives a request that match the root path of a registered redirection, it replies with a 301 permanent redirect indicating the full URI to use.

UDP port 70 is used for redirection registrations, because this port is assigned to the Gopher protocol and, let's be serious, who use Gopher nowadays?

A redirection message is a text that follows the syntax below:

      'REDIRECT' time ' ' [host:]port ' ' root-path [' ' signature]
      
where host is a host name or IP address, time is the system time when the message was formatted (see time(2)), port is a number in the range 1..65535 and root is an URI's absolute path. If the host is missing, houseportal uses the host name of the local machine. An optional cryptographic signature can be used to authenticate the source of the redirection. That signature is calculated over the text of the redirection message, excluding the signature portion. The signature is defined as a SHA-256 HMAC.

Houseportal will redirect to the specified port any request which absolute path starts with the specified root path. There is no response to the redirect message.

The registration must be periodic:
* This allows houseportal to detect applications that are no longer active.
* This allows redirections to recover from a houseportal restart.

In order to support applications not designed for houseportal, a static redirection configuration is supported:
* The redirection must be provided in /etc/default/houseportal (a text file).
* The content is a list of unsigned redirection messages, one redirection message per line.
* These static redirections never expire.

A simple form of security is possible by accepting only local UDP packets, i.e. houseportal to bind its UDP socket to IP address 127.0.0.1. This is typically used when all local applications are trusted, usually because the local machine's access is strictly restricted. That mode is activated when the LOCAL keyword is present in /etc/default/houseportal, using the syntax:

       'LOCAL'

To support security in a more open environment, /etc/default/houseportal may require the use of cryptographic signature:

       'SIGN' 'SHA-256' key [root-path]

Where the key is an hexadecimal string (64 bytes) that must be used by client when computing their signature. The optional root-path indicate that this key must be used for the specified root path only. The SIGN item may be repeated multiple times, with or without root-path: house-portal will try to use each applicable key until one authentifies the source successfully.

It is valid to combine both, i.e. activate the local mode and use cryptographic authentication.

If no cryptographic key is provided, houseportal will accept all redirection messages, with or without signature. If at least one cryptographic key is provided, houseportal will require every redirection message to be signed: if no signature matches, or if no key is applicable to the provided root path, the redirection message is ignored.
