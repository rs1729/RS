
## Tools

### convert decoder output

* `pos2kml.pl`, `pos2gpx.pl`, `pos2nmea.pl`, `pos2aprs.pl` <br />
  perl scripts for kml-, gpx-, nmea- or aprs-output, resp.
  #### Usage/Example
  `./rs92ecc --ecc --crc -v --vel2 -e brdc3050.15n 2015101_14Z.wav | ./pos2nmea.pl` <br />
  `stderr`: frame output <br />
  `stdout`: NMEA output <br />
  Only NMEA:
  `./rs92ecc --ecc --crc -v --vel2 -e brdc3050.15n 2015101_14Z.wav | ./pos2nmea.pl 2>/dev/null`


