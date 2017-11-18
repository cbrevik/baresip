README
------


This module implements an MQTT (Message Queue Telemetry Transport) client
for publishing and subscribing to topics.


The module is using libmosquitto


Starting the MQTT broker:

```
$ /usr/local/sbin/mosquitto -v
```


Subscribing to the topic:

```
$ mosquitto_sub -t baresip -q 1
```


Publishing to the topic:

```
$ mosquitto_pub -t baresip -m foo=42 -q 1
```


