# GPS logger

The software/hardware system provides a non cloud based GPS logger application, ideal for vehicle usage. It is not a "live" tracker, but a logger - providing automatic upload of logs when returning to base, via WiFi.

Main application is installed in vehicles, such as company car, etc, for automatic logging of all journeys without any manual intervention.

## Buy

Available now on [A&A web site](https://www.aa.net.uk/etc/circuit-boards/gps/)

Also, the PCBs are included here - if making your own, remove trademark logos.

## Logger board

This code is designed to work on the GPS Logger board (see PCB directory). The logger board connects to a GPS module which is based on the [Quectel L86](https://www.quectel.com/wp-content/uploads/pdfupload/Quectel_L86_GNSS_Specification_V1.3.pdf) GNSS, which is a powerful GPS module that can track NAVSTAR (US GPS), GLONASS (Russian GPS), and Galileo (EU GPS) Satellites.

The two boards can be directly connected to make a compact unit, or connected via a 5 core lead (or 4 core as PPS signal is not needed). This allows the smaller GPS module to be installed on a dash board or somewhere with a better view of the sky, and the main logger to be separate.

The logger board is powered vi a USB-C connector, but also supports a small LiPo battery, with built in battery charger, ideal for cases where USB is switched with car ignition.

An SD card is needed (up to 16GB) for logs to be stored until uploaded. Do not drop power whilst logging as log file will be lost (this may be improved in future). This is another reason for using a LiPo as well as USB power.

## Initial set up

The s/w uses the [RevK library](https://github.com/revk/ESP32-RevK) which provides a format for commands and settings over MQTT. It means it will provide a WiFi AP to allow initial config (if not automatically shown, access the *router* IP via web page once connected). This allows WiFi, MQTT, and the URL for log uploads.

Note: Remove SD card to enable WiFi, WiFi AP mode, and settings. While SD is inserted WiFi AP and settings are disabled, and WiFi only when stationary (and at home if `home` set).

## Log upload

When back on WiFi, and not moving, all log files on the SD card are uploaded as a POST to specified URL (which can be https if using known certificates, including Let's Encrypt). Once uploaded it is deleted from the SD card. If no URL is set, the file stays on the SD card and can be accesses using a card reader as needed.

The post includes a *query* string that is the MAC and start date/time as a filename. But this is included in the JSON data.

## Important settings

Settings can be sent via MQTT, as per the revK library, e.g. sending `setting/GPS/packdist 2` to set `packdist` to `2`. Settings can also be sent in JSON format. To see settings, send just `settings/GPS`.

|Setting|Meaning|
|-------|-------|
|`url`|Upload URL|
|`logpos`|If not set then the log file does not include position data at all|
|`loglla`|Include `lat`, `lon`, and `alt`|
|`logcs`|Include `course` and `speed`|
|`logund`|Include undulation `und`|
|`logmph`|Include `mph`|
|`logacc`|Include accelerometer data|
|`logsats`|Include number of active satellites and related data|
|`logepe`|Include estimated position error|
|`logodo`|Include periodic odometer value|
|`logseq`|Include a sequence number|
|`logecef`|Include ECEF data (Earth Centred Earth Fixed X/Y/Z/T values)|
|`logacc`|Include accelerometer data|
|`gpsballon`|Set *balloon* mode for high altitude|
|`gpsflight`|Set *flight* mode for aviation logging|
|`gpswalking`|Set *walking* mode for slow speed travel|
|`navstar`|Use NAVSTAR|
|`glonass`|Use GLONASS|
|`galileo`|Use GALILEO|
|`minmove`|Seconds moving before we start, if slow|
|`minstop`|Seconds not moving before we stop, if not home, to allow for traffic lights, etc|
|`packmin`|Min samples to be packed, normally `60`, set this to zero to disable packing|
|`packmax`|Max samples to be packed, normally `600` which means if travelling straight you get a sample at least this often|
|`packdist`|If non zero this enables point reduction *packing*, with this being the allowed deviation (in metres)|
|`packtime`|If *packing* and this is non zero then time is also a factor, with this being the allowed deviation in seconds|
|`home`|An array of ECEF whole metres for home location (use web interface settings to set this from GPS). If set WiFi only comes on at home|
|`homem`|Proximity to `home` for home working|

## LEDs

There is one LED by the SD card that shows the status of the SD card (on older boards this is first LED on LED strip);

|Colour|Meaning|
|------|-------|
|Red|Some problem with SD card|
|Magenta|Card not present|
|Yellow|Card mounted|
|Green|Card being written to - don't power off in this state if possible|
|Blue|Card unmounted, safe to remove|
|Cyan|Data being uploaded from card|

There is a string of LEDs to show satellite status, in order...

- If SBAS then a magenta LED shows
- Green LEDs show for NAVSTAR GPS, 2 sats per LED, last is dim if odd number
- Yellow LEDs show for GLONASS GPS, 2 sats per LED, last is dim if odd number
- Cyan LEDs show for GALILEO GPS, 2 sats per LED, last is dim if odd number
- If no active satellites and no SBAS a single red LED shows
- If there is less than a 3D fix the satellite LEDs blink

Also, the last LED is over written with RED if no GPS receiver, or ORANGE if stationary at home, or MAGENTA if stationary not at home (or home not set).

There are also special situations where the LEDs show a bar graph, this starts at the other end to avoid confusion with satellite status.

- Yellow for progress of software upgrade.
- Cyan for progress of log upload (per file).


## Point reduction

The optional *pack* logic uses a modified Ramer–Douglas–Peucker algorithm. It does not work on fixed time periods - allowing a `packmin` minimum samples, but up to `packmax` if no *corners* found. It also packs to the *corner*, and then considers from that with more points.

The effect is that if you go at constant speed in a straight line you may only see a point every 10 minutes. If you stop/start or turn, then that point is logged. Lost points are discarded if within a distance from a straight line that is logged, this allows detail and concise logs.

Point reduction on device is optional, and only if `packdist` is set. `packtime` being set (seconds) allows time to be included in the calculations. However, packing can be done as a port processing operation using the `json2gpx` tool.

## Log format

The log format is a simple JSON object.

|Top level fields|Meaning|
|----------------|-------|
|`start`|Start time of the log file|
|`end`|End time of the log file|
|`id`|MAC of logger|
|`version`|S/W version of logger|
|`distance`|Distance covered by log file from odometer readings|
|`gps`|Array of fix points|

The fix point data is generally self explanatory. `speed` is kph. `odo` and `alt` are metres. ECEF is metres. `lat`/`lon`/`course` are degrees.

Note that the odometer logic is internal to the L86, and may track distance travelled if stationary without clear satellite coverage (such as indoors). As such it makes sense to use this for a definite journey if not kept in good view of sky, or off when not in use.

Whilst location data (`lat`/`lon`/`alt`/`ecef`) is per fix, some data is slower, such as `course`, `speed`, `epe`, `vdop`/`pdop`, and active sats, and as such they do not change every fix.

## WiFi

WiFi is enabled when not moving. If a `home` location is set it is only enabled within 100m of home.

Normally access point mode, and web based settings are disabled. These are enabled, along with WiFI, when the SD card is removed.

--
Copyright © 2019-24 Andrews & Arnold Ltd, Adrian Kennard. See LICENSE file (GPL).
