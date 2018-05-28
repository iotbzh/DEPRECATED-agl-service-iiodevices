# Radio Service

## Overview

Accelera Service allows getting data from acceleration, gyroscope and
electronic compass.

## Verbs

| Name               | Description                                 | JSON Parameters                                                   |
|:-------------------|:-------------------- -----------------------|:---------------------------------------------------------------   |
| subscribe          | subscribe to accelero events                | *Request:* {"event": "accel"} {"event": "magn"} {"event":anglvel"}|
| unsubscribe        | unsubscribe to accelero events              | *Request:* {"event": "accel"} {"event": "magn"} {"event":anglvel"}|

## Events

There are 3 different events matching with the different sensor available.

* "accel": is for acceleration data
* "magn": is for gyroscope data
* "anglvel": is for  data

### frequency Event JSON Response

JSON response has a single field **frequency** which is the currently tuned frequency.

### station_found Event JSON Response

JSON response has a single field **value** of the frequency of the discovered radio station.

# AGL Radio Tuner Binding

## FM Band Plan Selection

The FM band plan may be selected by adding:
```
fmbandplan=X
```
to the [radio] section in /etc/xdg/AGL.conf, where X is one of the
following strings:

US = United States / Canada
JP = Japan
EU = European Union
ITU-1
ITU-2

Example:
```
[radio]
fmbandplan=JP
```

## Implementation Specific Confguration

### USB RTL-SDR adapter

The scanning sensitivity can be tweaked by adding:
```
scan_squelch_level=X
```
to the [radio] section in /etc/xdg/AGL.conf, where X is an integer.  Lower
values make the scanning more sensitive.  Default value is 140.

Example:
```
[radio]
scan_squelch_level=70
```

### M3ULCB Kingfisher Si4689

## Known Issues

### M3ULCB Kingfisher

