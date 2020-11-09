import sys
import netCDF4
from datetime import datetime

## Norwegian Meteorological Institute
## https://thredds.met.no/thredds/catalog/remotesensingradiosonde/catalog.html
##
## python3 metno_netcdf_gpx.py <station_data.nc>

def print_cdf(dat):
    station = dat.station_name + ' (' + dat.wmo_block_and_station_number + ')'
    station_lat = dat.station_latitude_degrees_north * 100.0  # ?
    station_lon = dat.station_longitude_degrees_east * 100.0  # ?
    station_alt = dat.station_altitude_meter
    print('')
    print('{0}  lat: {1:.5f} lon: {2:.5f} alt: {3:.1f}m'.format(station, station_lat, station_lon, station_alt) )
    ts = dat.variables['time'][:].data
    sn = dat.variables['serial_number'][:]
    rs = dat.variables['sounding_system_used'][:].data  # rs[]: BUFR 002011 , rs[]%100: r_ar_a 3685
    tls = dat.variables['time_from_launch'][:].data
    N = len(ts)
    for n in range(N):
        print('{0} # SN="{1}" # type={2:d}'.format(datetime.utcfromtimestamp( ts[n] ).strftime('%Y-%m-%dT%H:%M:%SZ'), sn[n], rs[n]) )
        fgpx = open("{0}.gpx".format(sn[n].replace(' ', '_')), "w")
        fgpx.write('<?xml version="1.0" encoding="UTF-8" standalone="no" ?>\n')
        fgpx.write('<gpx version="1.1" creator="me" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://www.topografix.com/GPX/1/1" xsi:schemaLocation="http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd">\n')
        fgpx.write('<metadata><desc>station: {0}, rs_type: {1}</desc></metadata>\n'.format(station, rs[n]) )
        fgpx.write('<trk><name>{0}</name><trkseg>\n'.format(sn[n]))
        lat = dat.variables['latitude'][n].data
        lon = dat.variables['longitude'][n].data
        alt = dat.variables['altitude'][n].data
        for k in range(len(alt)):
            if (alt[k] > -900.0):
                fgpx.write('<trkpt lat="{:.6f}" lon="{:.6f}"><ele>{:.2f}</ele>'.format(lat[k], lon[k], alt[k]) )
                fgpx.write('<time>{0}</time></trkpt>\n'.format(datetime.utcfromtimestamp( ts[n]+tls[k] ).strftime('%Y-%m-%dT%H:%M:%SZ')) )
        fgpx.write('</trkseg></trk>\n')
        fgpx.write('</gpx>')
        fgpx.close()



if len(sys.argv) < 2:
    sys.exit("error argv")

f = sys.argv[1]

data = netCDF4.Dataset(f)
print_cdf(data)


print('')


