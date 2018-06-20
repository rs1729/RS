
## Tools

  ### convert decoder output

  * `pos2kml.pl`, `pos2gpx.pl`, `pos2nmea.pl`, `pos2aprs.pl` <br />
  perl scripts for kml-, gpx-, nmea- or aprs-output, resp.
  * Usage/Example <br />
  `./rs92ecc --ecc --crc -v --vel2 -e brdc3050.15n 2015101_14Z.wav | ./pos2nmea.pl` <br />
  `stderr`: frame output <br />
  `stdout`: NMEA output <br />
  Only NMEA:
  `./rs92ecc --ecc --crc -v --vel2 -e brdc3050.15n 2015101_14Z.wav | ./pos2nmea.pl 2>/dev/null`


  ### audio output to stdout

  * `pa-stdout.c` <br />
  select SDR Output device/stereo channel and output to stdout <br />
  * Usage/Example <br />
  `./pa-stdout --list` <br />
  `./pa-stdout 2 | ./rs41dm_dft --ecc2 --crc -vx --ptu` <br />
  <i> ( audio device no 2 -> stdout -> ./rs41dm_dft ) </i>


