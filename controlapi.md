# The House Control Web API

A common API to control the physical world.

## Overview

This API gives access to physical world devices, regardless of the communication protocol or hardware interfaces used to access them. It provides two main features: report on the current state of devices and offer a way to control this state.

This API was designed to be as generic as possible. The goal is to reuse the same API for different classes of hardware.

Each device state is represented by a point. Every point is identified by a name that should be unique across the network, i.e. two separate service instances should not return the same point name. This allows clients to discover which servers offer access to the devices that they are controlling, without having to configure this manually.

A point has at least two possible states:

- off: the device is not active (provides no light, motor is not moving, etc).
- on: the device is active (provides light, motor is moving, etc.)

Points may also support the "alert" state, which indicates that the device is active ("on") but requires the operator's attention.

A pseudo state "silent" is returned when the physical device is not responding. This is a device failure state: the actual state of the device is unknown.

A point can be a controllable (a.k.a. "output") point or else an input point. The difference is that any device control request is ignored for input points. An input point is typically used when there is no way to control the physical device. For example a reed relay reports its status but there is no way to control it.

Each controllable point supports at least two device controls:

- off: turn the device off (regardless of its current state except "silent")
- on: turn the device on (regardless of its current state except "silent")

Controllable points may also support the following controls:

- alert: turn the point to the alert state
- clear: change the point state from "alert" to "on". This control has no effect if the point is not in the alert state.

Device controls may be latched (i.e. set permanently), or a pulse (i.e. set for a specified duration). A pulse control causes the point to return to the off state after the specified time has expired. Obviously the off control should always be latched. The benefit of a pulse is that the client application does not need to keep a context so that it can stop the device later on.

Each point has a `gear` attribute that matches the type of physical device associated with this point. That attribute is a string, with common values such as "light", "valve", "camera". The `gear` attribute can be used to filter points in a panel.

Points might support more states than listed above, but the additional states are application specific and might not be handled by all clients. An application may use the `gear` attribute to determine the list of states and controls that are applicable to the point.

## Poll for changes mechanism

This API supports the standard House poll for changes mechanism:

- The data returned by the service includes a `latest` field (numeric) that matches the content of the returned data.

- Subsequent requests repeat this value as the `known` parameter.

- If the `known` parameter still matches the service's current `latest` value, i.e. the response data has not changed, the service returns an HTTP status 304 and no response data.

- If the `known` parameter is not present or no longer matches the service's current `latest` value, the service returns an HTTP status 200 and the requested response data.

- Any change to the state of the service that impacts the response data (point state, configuration change, etc) causes the associated `latest` value to change.

> [!NOTE]
> Depending on the service, there can be a single state for all requests, or a separate state for each request (e.g. `status` or `config`). Separate states are not necessarily independent: for example a change to the configuration also impact the status data, and so the "status" state will change as well if the "config" state changes.

> [!NOTE]
> The `latest` field is _not_ a timestamp.

## Web API

> [!NOTE]
> the string `(service)` in the endpoint descriptions below means the actual root URI of the service. Each service implementation uses a unique root URI.

```
GET /(service)/status[?known=N]
```

Returns a status JSON object that lists each point by name. Each point is itself an object with the point state and other attributes.

If the `known` parameter is present and the status has not changed, the service returns HTTP code 304 and no response data.

The main status object has the following fields:

- `host`: the name of the computer that runs this service.
- `proxy`: the name of the computer that runs the HousePortal service used for this computer. This is typically the same as `host`.
- `timestamp`: the time when this data was generated.
- `latest`: the current state of the service. Not present if the service does not support the poll for changes mechanism.
- `control.status`: a JSON object that lists each point by name. A point element is itself a JSON object (see below).

Each point object has the following fields:

- `mode`: string "output" (controllable point) or "input". The default mode is "output" if this field is not present.
- `state`: the current state of the point, "off", "on", "alert" or more.
- `command`: the most recent control request. This can be different from the current state if the control is pending and not completed. This field is not present for input points and may not be present if its value is the same as the current state (i.e. no control request is pending).
- `pulse`: the current state's deadline. This is an absolute timestamp, not a duration. This field is not present if the current state is latched.
- `gear`: the type of physical device attached to this point. That field may be used to handle application specific states. This field is not present if no gear type was set in the service's configuration. Note that some services set the `gear` attribute to "light" and that field cannot be configured.
- `priority`: indicate the priority level for the current state. A control request with a "low" priority cannot override a current "high" priority state. Low priority is 0, high priority is 1. This field is not present if the service does not support control priority, or may not be present if the priority is low. See the `set` request below for more information.

```
GET /(service)/set?point=NAME&state=off|on|clear[&pulse=N][&cause=TEXT]
```

Request the specified control point to transition to the specified state. If the pulse parameter is present, the state is maintained for the specified number of seconds and then reverted to the "off" state when the control expires. If the pulse parameter is not present or its value is 0, the specified state is maintained until the next set request is issued.

The `cause` text is reflected in the events that record the point changes. This helps identifying which service requested the control, especially when this is a scheduled control, or a control issued based on some automated logic. In addition, some services use the `cause` parameter to decide on the control request's priority level: if the `cause` parameter is missing or set to "MANUAL", the priority is high; otherwise the priority is low. This priority is typically used to avoid an automated logic fighting with a human's manual control. For example if there is an automatism to turn a light on for a few minutes on specific condition, but a human operator turned the switch on manually, you do not want the automatism to turn the light off on the operator. This is particularly useful for devices such as wall switches, which can be operated directly.

```
GET /(service)/config[?known=N]
```

Returns the current configuration data in JSON. If the `known` parameter is present, this returns HTTP code 304 and no data if the configuration has not changed.

The format of the configuration data is specific to each service.

```
POST /(service)/config
```

Upload and activate a new service configuration. The format of the configuration data is specific to each service.

## Optional sequence of changes extension API

Some implementation of this control API may support recording sequences of changes. The purpose of this extension is to capture changes that occur faster than the poll period. A service that supports this extension keeps an internal history of the latest changes in memory, which is returned through this API extension. For example if the client polls every 2 seconds, but changes have been detected every 200 milliseconds, this API extension allows the client to get all 10 changes.

The history is only kept for input points. No change history is provided for controllable points. The history has a fixed depth, typically enough for storing 6 seconds worth of changes.

Input points are automatically scanned at a higher sampling rate while this API is actively used. This fast sampling scan rate stops when no request has been made for some time (typically 12 seconds). Since the first request starts the faster scan rate, this first request does not return any sequence of changes.

```
GET /(service)/changes[?since=MILLISECONDS][&sync=0|1]
```

Return a JSON array of the recent sequence of input state changes. The history is not saved to disk and the server keeps only a fixed number of state changes, typically 6 seconds worth of history. The client must request new changes at least every 5 seconds or else changes might be lost.

If the `since` option is present, only changes more recent than the specified timestamp are returned. That timestamp is in milliseconds.

If the `sync` option is present and its value is 1, the response also includes the state of all points (not just input) in the same `control.status` object format as returned by the standard status request. This option is intended to keep the sequence of changes data and the status data properly ordered. The client must process the sequence of changes data first, then the status data. (The sequence of changes data represents changes that occurred prior to the current status, while the status data represents the final state of the points at the time of the request.) Future changes requests will include changes subsequent to that status. This way the client will not process the sequence of changes out of order compared to status.

A client requesting both changes and status must use the `/(service)/changes` request instead of `/(service)/status`. The `sync` option must be used on the first request, and may be subsequently used at a longer interval than the poll (i.e. the sync option may be set every N requests only).

> [!NOTE]
> This is similar to how the [DNP 3](https://www.dnp.org/) standard keeps events and static polls synchronized. The main difference is that DNP 3 requires keeping the `since` context on the service side, i.e. on the outstation, while this API makes the client responsible for keeping that context. Keeping the context on the client side makes multi-clients support much simpler.

The JSON data returned contains the following elements:

- `host`:                   Name of the host machine this service runs on.
- `timestamp`:              Time of the response.
- `control.changes`:        An object that describes the input points that changed, or an empty object when no change history is available.
- `control.changes.start`:  Time of the oldest state.
- `control.changes.step`:   Interval between two consecutive values.
- `control.sequend.end`:    Time of the most recent state (relative to changes.start).
- `control.changes.data`:   An object that lists every input point that changed. Each input point is an array of sequential values (i.e. a time series) sampled at `control.changes.step` intervals.
- `control.status`:         If sync is requested. See the status request.

All time values in the control.changes object are in millisecond units.

The next request's `since` parameter value should be equal to `control.changes.start + control.sequend.end`.

