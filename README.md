# IIODEVICES Service

## Overview

iiodevices service allows getting data from iiodevices. For now it allows to
to get data from acceleration, gyroscope and electronic compass.

## General Scheme

As soon as a client subscribes to the agl-service-iiodevices binding,
the binding gets values from sensors and send it to subscribers as events.
Subscribers can choose their frequency and indicates what values they wants at
subscription. For now, subscribers have also to indicates an uid in order to
create differents events, if arguments differ between clients.

## Verbs

| Name               | Description                                 | JSON Parameters                                                   |
|:-------------------|:--------------------------------------------|:---------------------------------------------------------------   |
| subscribe          | subscribe to 9 axis events                  | *Request:* {"event": "accel", "uid": "X", "args":"xy", "frequency": "10" }|
| unsubscribe        | unsubscribe to accelero events              | *Request:* {"event": "accel", "uid": "X"} |

## Events

For now, there are 3 different events matching with the different available sensors.

* "accel": is for acceleration data
* "magn": is for gyroscope data
* "anglvel": is for electronic compass data

## Remaining issues

- Provide a json config file so that configures the device name and the channel name.
- Remove the uid json argument and using context_make to identify session and better handle events
- Rework on channel structure and split it into client structure
- Handle several values simultaneously, see trigger
- Update it to other iiodevices

## M3ULCB Kingfisher

M3ULCB Kingfisher is equipped with a 9 axis sensor device (LSM9DS0) and the R-Car Starter
Kit can read sensor value via I2C interface and iiodevices is provided for
these sensors.
