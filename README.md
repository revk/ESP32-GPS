# GPS logger

The software/hardware system provides a non cloud based GPS logger application, ideal for vehicle usage. It is not a "live" tracker, but a logger - providing automatic upload of logs when returning to base, via WiFi.

## Logger board

This code is designs to work on a GPS Logger board (see PCB directory). The logger board connects to a GPS module which is based on the [Quectel L86](https://www.quectel.com/wp-content/uploads/pdfupload/Quectel_L86_GNSS_Specification_V1.3.pdf) GNSS, which is a powerful GPS module that can track NAVSTAR (US GPS), GLONASS (Russian GPS), and Galileo (EU GPS) Satellites.

The two boards can be directly connected to make a compact unit, or connected via a 5 core lead (or 4 core as PPS signal is not needed). This allows the smaller GPS module to be installed on a dash board or somewhere with a better view of the sky, and the main logger to be separate.

The logger board is powered vi a USB-C connectot, but also supports a small LiPo battery, with built in battery charger, ideal for cases where USB is switched with car ignition.

An SD card is needed (up to 16GB) for logs to be stored until uploaded. Do not drop power whilst logging as log file will be lost (this may be improved in future). This is another reason for using a LiPo as well as USB power.

## Initial set up

The s/w uses the [RevK library](https://github.com/revk/ESP32-RevK) which provides a format for commands and settings over MQTT. It means it will provide a WiFi AP to allow initial config (if not automatically shown, access the *router* IP via web page once connected). This allows WiFi, MQTT, and the URL for log uploads.

## Log upload

When back on WiFi, and not moving, all log files on the SD card are uploaded as a POST to specified URL (which can be https if using known certificates, including Let's Encrypt). Once uploaded it is deleted from the SD card. If no URL is set, the file stays on the SD card and can be accesses using a card reader as needed.

## Important settings

|Setting|Meaning|
|-------|-------|
|`url`|Upload URL|
|`logcs`|Include `course` and `speed`|
|`logacc`|Include accelerometer data|
|`logsats`|Include number of active satellites and related data|
|`logepe`|Include estimated position error|
|`logodo`|Include periodic odometer value|
|`logseq`|Include a sequence number|
|`logecef`|Include ECEF data (Earth Centred Earth Fixed X/Y/Z/T values)|
|`gpsballon`|Set *balloon* mode for high altitude|
|`gpsflight`|Set *flight* mode for aviation logging|
|`gpswalking`|Set *walking* mode for slow speed travel|
|`navstar`|Use NAVSTAR|
|`glonass`|Use GLONASS|
|`galileo`|Use GALILEO|
|`moven`|How many VTG samples (10 second) have to have a non zero speed to start moving, or move if higher than `hepe`|
|`stopn`|How many VTG samples (10 second) have to be zero to be stopped, or zero speed when back on WiFi and logs flushed|
|`packmin`|Min samples to be packed, normally `60`|
|`packmax`|Max samples to be packed, normally `600` which means if travelling straight you get a sample at least this often|
|`packm`|Discard points that are within this many metres of straight line, `0` means don't pack|
|`packs`|Discard points taht are within this many seconds of constant speed, `0` means ignore time when packing|
|`packe`|If set, reduce distance by `hepe` when packing, means lower quality points tend to get discarded|

## LEDs

There are a string of LEDs, more LEDs means more active satellites. There is also an LED for the SD card.

## Point reduction

The *pack* logic uses a modified Ramer–Douglas–Peucker algorithm, which can use 4 dimensions (time as well), and adjust for `hepe`. It also does not work on fixed time periods - allowing a `packmin` minimum samples, but up to `packmax` if no *corners* found. It also packs to the *corner*, and then considers from that with more points.

The effect is that if you go at constant speed in a straight line you may only see a point every 10 minutes. If you stop/start or turn, then that point is logged. Lost points are discarded if within a distance from a straight line that is logged, this allows detail and consise logs.

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

The fix point data is generally self explanatory. `speed` is metres/second. `odo` and `alt` are metres. ECEF is metres. `lat`/`lon`/`course` are degrees.

Note that the odometer logic is internal to the L86, and may track distance travelled if stationary without clear satellite coverage (such as indoors). As such it makes sense to use this for a definite journey if not kept in good view of sky, or off when not in use.

--
Copyright © 2019-24 Andrews & Arnold Ltd, Adrian Kennard. See LICENSE file (GPL).
