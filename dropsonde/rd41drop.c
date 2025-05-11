
/*
  dropsonde RD41
  frames,position: 2Hz
  velocity(wind): 4Hz
*/

#include <stdio.h>
#include <string.h>
#include <math.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;

int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_inv = 0,      // invertiert Signal
    fileloaded = 0,
    option_res = 0,
    rawin = 0;

typedef struct {
    int frnr;
    unsigned id;
    int week; int gpstow;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; int sek; int ms;
    double lat; double lon;
    double alt;
    //
    double   X; double   Y; double   Z;
    double pAcc;
    double vX1; double vY1; double vZ1;
    double sAcc1;
    int sats1;
    double vN; double vE; double vU;
    double vH; double vD; double vV;
    //
    double vX2; double vY2; double vZ2;
    double sAcc2;
    int sats2;
    double alt2;
    double vN2; double vE2; double vU2;
    double vH2; double vD2; double vV2;
    //
    double P; double T; double U1; double U2;
    double bat; double iT;
} gpx_t;

gpx_t gpx;

#define BITS (1+8+1)  // 8N1 = 10bit/byte

#define HEADLEN (40)
#define HEADOFS (40)

char header[] =
"10100110010110101001"   // 0x1A = 0 01011000 1
"10010101011010010101"   // 0xCF = 0 11110011 1
"10101001010101010101"   // 0xFC = 0 00111111 1
"10011001010110101001";  // 0x1D = 0 10111000 1
//"10011010101010101001" // 0x01 = 0 10000000 1

char buf[HEADLEN+1] = "x";
int bufpos = -1;

#define       FRAME_LEN  120  // 240/sec -> 120/frame
#define    BITFRAME_LEN (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (FRAME_LEN*BITS*2)

char frame_rawbits[RAWBITFRAME_LEN+8];
char frame_bits[BITFRAME_LEN+4];
ui8_t frame_bytes[FRAME_LEN+10];


/* ------------------------------------------------------------------------------------ */

#define BAUD_RATE 4800

int sample_rate = 0, bits_sample = 0, channels = 0;
float samples_per_bit = 0;

int findstr(char *buf, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buf[(pos+i)%4] != str[i]) break;
    }
    return i;
}

int read_wav_header(FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4)) return -1;
    // pos_fmt = 12L
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;

    if (fread(dat, 1, 2, fp) < 2) return -1;
    channels = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, dat, 4); //sample_rate = dat[0]|(dat[1]<<8)|(dat[2]<<16)|(dat[3]<<24);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;
    //byte = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);

    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    samples_per_bit = sample_rate/(float)BAUD_RATE;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}


#define EOF_INT  0x1000000

int read_signed_sample(FILE *fp) {  // int = i32_t
    int byte, i, ret;         //  EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
                           // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF_INT;
        if (i == 0) ret = byte;

        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF_INT;
            if (i == 0) ret +=  byte << 8;
        }

    }

    if (bits_sample ==  8) return ret-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16) return (short)ret;

    return ret;
}

int par=1, par_alt=1;
unsigned long sample_count = 0;

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    static int sample;
    int n, y0;
    float l, x1;
    static float x0;

    n = 0;
    do {
        y0 = sample;
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        sample_count++;
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        n++;
    } while (par*par_alt > 0);

    if (!option_res) l = (float)n / samples_per_bit;
    else {                                 // genauere Bitlaengen-Messung
        x1 = sample/(float)(sample-y0);    // hilft bei niedriger sample rate
        l = (n+x0-x1) / samples_per_bit;   // meist mehr frames (nicht immer)
        x0 = x1;
    }

    *len = (int)(l+0.5);

    if (!option_inv) *bit = (1+par_alt)/2;  // oben 1, unten -1
    else             *bit = (1-par_alt)/2;  // sdr#<rev1381?, invers: unten 1, oben -1
// *bit = (1+inv*par_alt)/2; // ausser inv=0

    return 0;
}

/* ------------------------------------------------------------------------------------ */


void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

int compare() {
    int i, j;

    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADOFS+HEADLEN-1-i]) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return 1;
/*
    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != cb_inv(header[HEADOFS+HEADLEN-1-i])) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return -1;
*/
    return 0;

}

// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
void manchester2(char* frame_rawbits, char *frame_bits) {
    int i;
    char bit, bits[2];

    for (i = 0; i < BITFRAME_LEN; i++) {
        bits[0] = frame_rawbits[2*i];
        bits[1] = frame_rawbits[2*i+1];

        if ((bits[0] == '0') && (bits[1] == '1')) bit = '1';
        else
        if ((bits[0] == '1') && (bits[1] == '0')) bit = '0';
        else bit = 'x';
        frame_bits[i] = bit;
    }
}

int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN) {

        byteval = 0;
        d = 1;
        for (i = 1; i < BITS-1; i++) {
            bit=*(bitstr+bitpos+i); /* little endian */
            //bit=*(bitstr+bitpos+BITS-1-i);  /* big endian */
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval;

    }

    //while (bytepos < FRAME_LEN) bytes[bytepos++] = 0;

    return 0;
}


/* ------------------------------------------------------------------------------------ */
/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
void Gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day) {

    long GpsDays, Mjd;
    long J, C, Y, M;

    GpsDays = GpsWeek * 7 + (GpsSeconds / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    *Day = J - 2447 * M / 80;
    J = M / 11;
    *Month = M + 2 - (12 * J);
    *Year = 100 * (C - 49) + Y + J;
}
/* ------------------------------------------------------------------------------------ */


#define OFS             (0x02)  // HEADLEN/(2*BITS)
#define pos_FrameNb     (OFS+0x01)   // 2 byte
// ublox5 NAV-SOL
//#define pos_GPSTOW      (OFS+0x18)   // iTow 4 byte (+ fTOW? 4 byte)
#define pos_GPSTOW      (OFS+0x20)    // iTow 4 byte (+ fTOW? 4 byte)
#define pos_GPSweek     (OFS+0x20)    // 2 byte
#define pos_GPSecefX    (OFS+0x24)    // 4 byte
#define pos_GPSecefY    (OFS+0x28)    // 4 byte
#define pos_GPSecefZ    (OFS+0x2C)    // 4 byte
#define pos_GPSpAcc     (OFS+0x30)    // 4 byte
#define pos_GPSecefV1   (OFS+0x34)    // 3*4 byte
#define pos_GPSsAcc1    (OFS+0x40)    // 4 byte
#define pos_GPSsats1    (OFS+0x46)    // 1 byte
#define pos_GPSecefV2   (OFS+0x4A)    // 3*4 byte
#define pos_GPSsAcc2    (OFS+0x56)    // 4 byte
#define pos_GPSsats2    (OFS+0x5A)    // 1 byte
// PTUsensors
#define pos_sensorP     (OFS+0x05)   // 4 byte float32
#define pos_sensorT     (OFS+0x09)   // 4 byte float32
#define pos_sensorU1    (OFS+0x0D)   // 4 byte float32
#define pos_sensorU2    (OFS+0x11)   // 4 byte float32
// internal
#define pos_sensorTi    (OFS+0x6C)   // 4 byte float32
#define pos_ID          (OFS+0x64)   // 4 byte
#define pos_rev         (OFS+0x70)   // 2 byte char? // e.g. "A5"
#define pos_bat         (OFS+0x6A)   // 2 byte
// checksums
#define pos_chkFrNb     (pos_FrameNb-1   +  3)  // 16 bit
#define pos_chkPTU      (pos_sensorP     + 17)  // 16 bit
#define pos_chkGPS1     (pos_GPSTOW      + 45)  // 16 bit
#define pos_chkGPS2     (pos_GPSecefV2-1 + 18)  // 16 bit
#define pos_chkInt      (pos_ID          + 21)  // 16 bit

#define pos_pckFrm      (OFS+0x00)   //  3 bytes
#define pos_pckPTU      (OFS+0x05)   // 16 bytes
#define pos_CCC         (OFS+0x17)   // 17 bytes
#define pos_DDD         (OFS+0x2A)   // 12 bytes
#define pos_EEE         (OFS+0x38)   // 13 bytes
#define pos_FFF         (OFS+0x47)   // 27 bytes
#define pos_pckIDint    (OFS+0x64)   // 14 bytes


unsigned check16(ui8_t *bytes, int len) {
    unsigned sum1, sum2;
    int i;
    sum1 = sum2 = 0;
    for (i = 0; i < len; i++) {
        sum1 = (sum1 + bytes[i]) % 0x100;
        sum2 = (sum2 + sum1) % 0x100;
    }
    //return sum1 | (sum2<<8);
    return sum2 | (sum1<<8);
}

unsigned crc16(ui8_t *bytes, int len) {
    int crc16poly = 0x1021;
    int rem = 0x0000, i, j;
    int byte;

    for (i = 0; i < len; i++) {
        byte = bytes[i];
        rem = rem ^ (byte << 8);
        for (j = 0; j < 8; j++) {
            if (rem & 0x8000) {
                rem = (rem << 1) ^ crc16poly;
            }
            else {
                rem = (rem << 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}

int get_ID() {
    int i;
    unsigned byte;

    byte = 0;
    for (i = 0; i < 4; i++) {         // big endian
        byte |= frame_bytes[pos_ID + i] << (24-8*i);
    }
    gpx.id = byte;

    return 0;
}

int get_FrameNb() {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[4];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[pos_FrameNb + i];
        frnr_bytes[i] = byte;
    }
    frnr = frnr_bytes[1] + (frnr_bytes[0] << 8);
    gpx.frnr = frnr;

    return 0;
}

int get_GPSweek() {
    int i;
    unsigned byte;
    ui8_t gpsweek_bytes[2];
    int gpsweek;

    for (i = 0; i < 2; i++) {
        byte = frame_bytes[pos_GPSweek + i];
        gpsweek_bytes[i] = byte;
    }

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
    if (gpsweek < 0) { gpx.week = -1; return 0x0300; }
    gpx.week = gpsweek;

    return 0;
}

char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

int get_GPStime() {

    gpx.std = frame_bytes[pos_CCC+ 9] & 0x1F;
    gpx.min = frame_bytes[pos_CCC+10];
    gpx.sek = frame_bytes[pos_CCC+11];
    gpx.ms  = frame_bytes[pos_CCC+12]*10;

    return 0;
}

int get_GPSlatlon() {
    double lat, lon, alt;
    int lat_i4, lon_i4;

    lat_i4 = (frame_bytes[pos_DDD+0+ 0]<<24) | (frame_bytes[pos_DDD+0+ 1]<<16) | (frame_bytes[pos_DDD+0+ 2]<<8) | frame_bytes[pos_DDD+0+ 3];
    lat = lat_i4 / 1e7;

    lon_i4 = (frame_bytes[pos_DDD+4+ 0]<<24) | (frame_bytes[pos_DDD+4+ 1]<<16) | (frame_bytes[pos_DDD+4+ 2]<<8) | frame_bytes[pos_DDD+4+ 3];
    lon = lon_i4 / 1e7;

    gpx.lat = lat;
    gpx.lon = lon;

    return 0;
}

int get_GPSvelalt() {
    short vH1, _D1, _V1; // 16bit
    short vH2, _D2, _V2; // 16bit
    int alt1_i3; // 24bit
    int alt2_i3; // 24bit
    double dir;

    // vel1
    vH1 = (frame_bytes[pos_CCC + 0] << 8) | frame_bytes[pos_CCC + 1];
    _D1 = (frame_bytes[pos_CCC + 2] << 8) | frame_bytes[pos_CCC + 3];
    _V1 = (frame_bytes[pos_CCC + 4] << 8) | frame_bytes[pos_CCC + 5];

    // alt1
    alt1_i3 = (frame_bytes[pos_CCC+6+ 0]<<16) | (frame_bytes[pos_CCC+6+ 1]<<8) | frame_bytes[pos_CCC+6+ 2];
    if (alt1_i3 & 0x800000) alt1_i3 -= 0x1000000;

    //vel2
    vH2 = (frame_bytes[pos_EEE + 0] << 8) | frame_bytes[pos_EEE + 1];
    _D2 = (frame_bytes[pos_EEE + 2] << 8) | frame_bytes[pos_EEE + 3];
    _V2 = (frame_bytes[pos_EEE + 4] << 8) | frame_bytes[pos_EEE + 5];

    // alt2
    alt2_i3 = (frame_bytes[pos_EEE+6+ 0]<<16) | (frame_bytes[pos_EEE+6+ 1]<<8) | frame_bytes[pos_EEE+6+ 2];
    if (alt2_i3 & 0x800000) alt2_i3 -= 0x1000000;

    gpx.vH = vH1/100.0;
    gpx.vD = _D1/100.0;
    gpx.vV = -_V1/100.0;
    gpx.alt = alt1_i3/100.0;

    gpx.vH2 = vH2/100.0;
    gpx.vD2 = _D2/100.0;
    gpx.vV2 = -_V2/100.0;
    gpx.alt2 = alt2_i3/100.0;

    return 0;
}

#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

double a = EARTH_a,
       b = EARTH_b,
       a_b = EARTH_a2_b2,
       e2  = EARTH_a2_b2 / (EARTH_a*EARTH_a),
       ee2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);

void ecef2elli(double X[], double *lat, double *lon, double *h) {
    double phi, lam, R, p, t;

    lam = atan2( X[1] , X[0] );

    p = sqrt( X[0]*X[0] + X[1]*X[1] );
    t = atan2( X[2]*a , p*b );

    phi = atan2( X[2] + ee2 * b * sin(t)*sin(t)*sin(t) ,
                 p - e2 * a * cos(t)*cos(t)*cos(t) );

    R = a / sqrt( 1 - e2*sin(phi)*sin(phi) );
    *h = p / cos(phi) - R;

    *lat = phi*180/M_PI;
    *lon = lam*180/M_PI;
}


int get_GPSkoord_rd94() {
    int i, k;
    unsigned byte;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double X[3], lat, lon, h;

    for (k = 0; k < 3; k++) {

        for (i = 0; i < 4; i++) {
            byte = frame_bytes[pos_GPSecefX + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        X[k] = XYZ / 100.0;

    }

    // ECEF-Position
    ecef2elli(X, &lat, &lon, &h);
    gpx.lat = lat;
    gpx.lon = lon;
    gpx.alt = h;

    for (i = 0; i < 4; i++) {
        byte = frame_bytes[pos_GPSpAcc + i];
        XYZ_bytes[i] = byte;
    }
    memcpy(&XYZ, XYZ_bytes, 4);
    gpx.pAcc = XYZ / 100.0;
    gpx.X = X[0];
    gpx.Y = X[1];
    gpx.Z = X[2];

    if ((h < -1000) || (h > 80000)) return 0x0200;

/*
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 4; i++) {
            byte = frame_bytes[pos_GPSecefV1 + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        V[k] = XYZ / 100.0;
    }

    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    gpx.vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    gpx.vE = -V[0]*sin(lam) + V[1]*cos(lam);
    gpx.vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    // NEU -> HorDirVer
    gpx.vH = sqrt(gpx.vN*gpx.vN+gpx.vE*gpx.vE);
//
    alpha = atan2(gpx.vN, gpx.vE)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                          // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;                 // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx._vD = dir;
//
    dir = atan2(gpx.vE, gpx.vN) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx.vD = dir;
*/
    return 0;
}

int get_GPSvel_rd94() {
    int i, k;
    unsigned byte;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double V[3];
    double phi, lam, dir;


    for (k = 0; k < 3; k++) {
        for (i = 0; i < 4; i++) {
            byte = frame_bytes[pos_GPSecefV1 + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        V[k] = XYZ / 100.0;
    }
    gpx.vX1 = V[0];
    gpx.vY1 = V[1];
    gpx.vZ1 = V[2];
    gpx.sats1 = frame_bytes[pos_GPSsats1];

    phi = gpx.lat*M_PI/180.0;
    lam = gpx.lon*M_PI/180.0;
    gpx.vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    gpx.vE = -V[0]*sin(lam) + V[1]*cos(lam);
    gpx.vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    gpx.vH = sqrt(gpx.vN*gpx.vN+gpx.vE*gpx.vE);
    dir = atan2(gpx.vE, gpx.vN) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx.vD = dir;

    for (i = 0; i < 4; i++) {
        byte = frame_bytes[pos_GPSsAcc1 + i];
        XYZ_bytes[i] = byte;
    }
    memcpy(&XYZ, XYZ_bytes, 4);
    gpx.sAcc1 = XYZ / 100.0;


    for (k = 0; k < 3; k++) {
        for (i = 0; i < 4; i++) {
            byte = frame_bytes[pos_GPSecefV2 + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        V[k] = XYZ / 100.0;
    }
    gpx.vX2 = V[0];
    gpx.vY2 = V[1];
    gpx.vZ2 = V[2];
    gpx.sats2 = frame_bytes[pos_GPSsats2];

    for (i = 0; i < 4; i++) {
        byte = frame_bytes[pos_GPSsAcc2 + i];
        XYZ_bytes[i] = byte;
    }
    memcpy(&XYZ, XYZ_bytes, 4);
    gpx.sAcc2 = XYZ / 100.0;


    return 0;
}

float float32_rd94(unsigned idx) {
    int i;
    unsigned num, val;
    float f;
    // double e, s, m;

    num = 0;
    for (i=0;i<4;i++) { num |= frame_bytes[idx+i] << (24-8*i); }
/*
    val = 0;
    for (i=31;i>=24;i--) { val |= ((num>>i)&1)<<(i-24); }
    e = (double)val-127;  // exponent

    val = 0;
    for (i=22;i>= 0;i--) { val |= ((num>>i)&1)<<i; }
    m = (double)val/(1<<23);  // mantissa

    s = (num>>23)&1 ? -1.0 : +1.0 ;  // sign

    f = s*(1+m)*pow(2,e);
*/
    val  = (num     &   0x800000)<<8;  // sign
    val |= (num>>1) & 0x7F800000;      // exponent
    val |=  num     &   0x7FFFFF;      // mantissa

    memcpy(&f, &val, 4);

    return f;
}
float float32(unsigned idx) {
    int i;
    unsigned num, val;
    float f;

    num = 0;
    for (i=0;i<4;i++) { num |= frame_bytes[idx+i] << (8*i); }

    memcpy(&f, &num, 4);

    return f;
}

int get_Sensors1() {

    gpx.P  = float32(pos_sensorP);
    gpx.T  = float32(pos_sensorT);
    gpx.U1 = float32(pos_sensorU1);
    gpx.U2 = float32(pos_sensorU2);

    return 0;
}

int get_Sensors2() {
    int val;

    gpx.iT = float32(pos_sensorTi);

    val = frame_bytes[pos_bat]<<8 | (frame_bytes[pos_bat+1]);
    gpx.bat = val/1e3;

    return 0;
}


int getBlock_FrNb(){  // block 0: frame counter
    unsigned bytes;
    unsigned crc;
    int chk = 0;

     // header (next frame)
    if ( frame_bytes[OFS+116] != 0x1A ) {
        chk |= (0x1 << 6);
    }
    if ( frame_bytes[OFS+117] != 0xCF ) {
        chk |= (0x1 << 7);
    }

    bytes = crc16(frame_bytes+pos_pckFrm, 3);
    crc = (frame_bytes[pos_pckFrm+3]<<8) | frame_bytes[pos_pckFrm+4];
    if (bytes != crc)  chk |= (0x1 << 0);

    get_FrameNb();

    return chk;
}

int getBlock_PTU(){  // block 1: sensors P, T, U1, U2
    unsigned bytes;
    unsigned crc;
    int chk = 0;

    bytes = crc16(frame_bytes+pos_pckPTU, 16);
    crc = (frame_bytes[pos_pckPTU+16]<<8) | frame_bytes[pos_pckPTU+17];
    if (bytes != crc)  chk |= (0x1 << 1);

    get_Sensors1();

    return chk;
}

int getBlock_GPS(){  // block 2,3: GPS pos+vel1, vel2
    unsigned bytes;
    unsigned crc;
    int chk = 0, err = 0;

    bytes = crc16(frame_bytes+pos_CCC, 17);
    crc = (frame_bytes[pos_CCC+17]<<8) | frame_bytes[pos_CCC+18];
    if (bytes != crc)  chk |= (0x1 << 2);

    bytes = crc16(frame_bytes+pos_DDD, 12);
    crc = (frame_bytes[pos_DDD+12]<<8) | frame_bytes[pos_DDD+13];
    if (bytes != crc)  chk |= (0x1 << 3);

    bytes = crc16(frame_bytes+pos_EEE, 13);
    crc = (frame_bytes[pos_EEE+13]<<8) | frame_bytes[pos_EEE+14];
    if (bytes != crc)  chk |= (0x1 << 4);

    bytes = crc16(frame_bytes+pos_FFF, 27);
    crc = (frame_bytes[pos_FFF+27]<<8) | frame_bytes[pos_FFF+28];
    if (bytes != crc)  chk |= (0x1 << 5);

    //err |= get_GPSweek();
    err |= get_GPStime();
    err |= get_GPSlatlon(); //get_GPSkoord();
    err |= get_GPSvelalt();

    return chk;  // | (err<<8);
}

int getBlock_Int(){  // block 4: SondeID, internalTemp, battery
    unsigned bytes;
    unsigned crc;
    int chk = 0;

    bytes = crc16(frame_bytes+pos_pckIDint, 14);
    crc = (frame_bytes[pos_pckIDint+14]<<8) | frame_bytes[pos_pckIDint+15];
    if (bytes != crc)  chk |= (0x1 << 6);

    get_ID();

    //if (option_verbose)
    get_Sensors2();

    return chk;
}


void print_frame() {
    int i, err=0;
    unsigned chk=0;

    if (option_raw) {
        for (i = 0; i < FRAME_LEN; i++) {
            fprintf(stdout, "%02x", frame_bytes[i]);
            //fprintf(stdout, "%02X ", frame_bytes[i]);
            if (option_raw == 2) {
              if ( i==OFS-1
/*
                || i==OFS+0  || i==OFS+2                            // frame-counter
                || i==OFS+4  || i==OFS+8 || i==OFS+12 || i==OFS+16  // sensors (P,T,U1,U2)
                || i==OFS+20 || i==OFS+21
                || i==OFS+23 || i==OFS+27                           // TOW
                || i==OFS+31 || i==OFS+33                           // week
                || i==OFS+35 || i==OFS+39 || i==OFS+43              // ECEF-pos
                || i==OFS+47
                || i==OFS+51 || i==OFS+55 || i==OFS+59              // ECEF-vel1
                || i==OFS+63 || i==OFS+66
                || i==OFS+67 || i==OFS+68                           // sats-1
                || i==OFS+72
                || i==OFS+73 || i==OFS+77 || i==OFS+81              // ECEF-vel2
                || i==OFS+85
                || i==OFS+89 || i==OFS+90                           // sats-2
                || i==OFS+92 || i==OFS+96 || i==OFS+98              // SondeID, Rev?
                || i==OFS+101                                       // bat
                || i==OFS+103 || i==OFS+107                         // internT
                || i==OFS+113 || i==OFS+115
*/
                 ) fprintf(stdout, "  ");

              if ( i==pos_pckFrm +3-1)  fprintf(stdout, " ");
              if ( i==pos_pckFrm +3+1)  fprintf(stdout, " [%04X]  ", crc16(frame_bytes+pos_pckFrm, 3));
              if ( i==pos_pckPTU +16-1) fprintf(stdout, " ");
              if ( i==pos_pckPTU +16+1) fprintf(stdout, " [%04X]  ", crc16(frame_bytes+pos_pckPTU, 16));
              if ( i==pos_CCC +17-1) fprintf(stdout, " ");
              if ( i==pos_CCC +17+1) fprintf(stdout, " [%04X]  ", crc16(frame_bytes+pos_CCC, 17));
              if ( i==pos_DDD +12-1) fprintf(stdout, " ");
              if ( i==pos_DDD +12+1) fprintf(stdout, " [%04X]  ", crc16(frame_bytes+pos_DDD, 12));
              if ( i==pos_EEE +13-1) fprintf(stdout, " ");
              if ( i==pos_EEE +13+1) fprintf(stdout, " [%04X]  ", crc16(frame_bytes+pos_EEE, 13));
              if ( i==pos_FFF +27-1) fprintf(stdout, " ");
              if ( i==pos_FFF +27+1) fprintf(stdout, " [%04X]  ", crc16(frame_bytes+pos_FFF, 27));
              if ( i==pos_pckIDint +14-1) fprintf(stdout, " ");
              if ( i==pos_pckIDint +14+1) fprintf(stdout, " [%04X]  ", crc16(frame_bytes+pos_pckIDint, 14));
            }
        }

        // chk = checkBlocks();
        if (option_raw == 2) {
            err = 0;
            err |= getBlock_FrNb();
            err |= getBlock_PTU();
            err |= getBlock_GPS();
            err |= getBlock_Int();
            chk = err & 0x7F;
            fprintf(stdout, "  # check: ");  // blocks: 0=F, 1=S, 2=G1, 3=G2, 4=G3, 5=G4, 6=I
            for (i = 0; i < 7; i++) fprintf(stdout, "%d", (chk>>i)&1);
            //fprintf(stdout, "_");
            //for (i = 6; i < 8; i++) fprintf(stdout, "%d", (chk>>i)&1);
        }

        fprintf(stdout, "\n");
    }
    else {

        err = 0;
        err |= getBlock_FrNb();
        err |= getBlock_PTU();
        err |= getBlock_GPS();
        err |= getBlock_Int();

        if (! (err & 0x3F) )
        {
            //Gps2Date(gpx.week, gpx.gpstow/1000, &gpx.jahr, &gpx.monat, &gpx.tag);
            fprintf(stdout, "[%5d] ", gpx.frnr);
            //fprintf(stdout, "%s ", weekday[gpx.wday]);
            //fprintf(stdout, " %04d-%02d-%02d", gpx.jahr, gpx.monat, gpx.tag);
            fprintf(stdout, " %02d:%02d:%02d.%03d",  gpx.std, gpx.min, gpx.sek, gpx.ms);
            //if (option_verbose) fprintf(stdout, " (W %d)", gpx.week);
            fprintf(stdout, "  ");
            fprintf(stdout, " lat: %.5f° ", gpx.lat);
            fprintf(stdout, " lon: %.5f° ", gpx.lon);
            fprintf(stdout, " alt: %.2fm ", gpx.alt);
            if (option_verbose == 3) {
                #if 0
                //fprintf(stdout," (%7.2f,%7.2f,%7.2f) ", gpx.X, gpx.Y, gpx.Z);
                //fprintf(stdout, " (E:%.2fm) ", gpx.pAcc);
                //fprintf(stdout," V1: (%5.2f,%5.2f,%5.2f) ", gpx.vX1, gpx.vY1, gpx.vZ1);
                //fprintf(stdout, "(E:%.2fm/s) ", gpx.sAcc1);
                #endif
            }
            //if (option_verbose) fprintf(stdout," sats: %2d ", gpx.sats1);
            if (option_verbose) {
                fprintf(stdout," vH: %.1fm/s  D: %.1f°  vV: %.1fm/s ", gpx.vH, gpx.vD, gpx.vV);
                //fprintf(stdout," ENU=(%.2f,%.2f,%.2f) ", gpx.vE, gpx.vN, gpx.vU);
            }
            if (option_verbose == 2) {
                fprintf(stdout, " alt2: %.2fm ", gpx.alt2);
                fprintf(stdout," vH2: %.1fm/s  D2: %.1f°  vV2: %.1fm/s ", gpx.vH2, gpx.vD2, gpx.vV2);
                //fprintf(stdout," ENU2=(%.2f,%.2f,%.2f) ", gpx.vE2, gpx.vN2, gpx.vU2);
                if (option_verbose == 3) {
                    #if 0
                    fprintf(stdout," V2: (%5.2f,%5.2f,%5.2f) ", gpx.vX2, gpx.vY2, gpx.vZ2);
                    fprintf(stdout, "(E:%.2fm/s) ", gpx.sAcc2);
                    fprintf(stdout," sats: %2d ", gpx.sats2);
                    #endif
                }
            }

            fprintf(stdout, "  ");
            fprintf(stdout, " P=%.2fhPa ", gpx.P);
            fprintf(stdout, " T=%.2f°C ",  gpx.T);
            fprintf(stdout, " H1=%.2f%% ", gpx.U1);
            fprintf(stdout, " H2=%.2f%% ", gpx.U2);
            fprintf(stdout, " ");
            fprintf(stdout, " (%09d) ", gpx.id);

            if (option_verbose) {
                fprintf(stdout, " ");
                fprintf(stdout, " Ti=%.2f°C ", gpx.iT);
                fprintf(stdout, " Bat=%.2fV ", gpx.bat);
            }

            chk = err & 0x7F;
            fprintf(stdout, "  # check: ");  // blocks: 0=F, 1=S, 2=G1, 3=G2, 4=G3, 5=G4, 6=I
            for (i = 0; i < 7; i++) fprintf(stdout, "%d", (chk>>i)&1);

            fprintf(stdout, "\n");  // fflush(stdout);
        }
    }
}

void print_bitframe(int len) {
    int i;
    for (i = len; i < RAWBITFRAME_LEN; i++) frame_rawbits[i] = '0';
    manchester2(frame_rawbits, frame_bits);
    bits2bytes(frame_bits, frame_bytes);
    print_frame();
}


int main(int argc, char **argv) {

    FILE *fp = NULL;
    char *fpname;
    char *pbuf = NULL;
    int header_found = 0;
    int i, pos, bit, len;

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!fileloaded)) {
        if ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] <file>\n", fpname);
            fprintf(stderr, "  file: audio.wav or raw_data\n");
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v,        (verbose)\n");
            fprintf(stderr, "       -r,        (output: rawbytes)\n");
            fprintf(stderr, "       -R,        (output: raw_bytes)\n");
            fprintf(stderr, "       -i         (invert polarity)\n");
            fprintf(stderr, "       --inbits   (input: bits)\n");
            fprintf(stderr, "       --inbytes  (input: bytes)\n");
            return 0;
        }
        else if ( strcmp(*argv, "-v") == 0 ) {
            option_verbose = 1;
        }
        else if ( strcmp(*argv, "-vv") == 0 ) {
            option_verbose = 2;
        }
        else if ( strcmp(*argv, "-r") == 0 ) {
            option_raw = 1;
        }
        else if ( strcmp(*argv, "-R") == 0 ) {
            option_raw = 2;
        }
        else if ( strcmp(*argv, "-i") == 0 ) {
            option_inv = 1;
        }
        else if (strcmp(*argv, "--inbits") == 0) { rawin = 1; }     // rawbits  input
        else if (strcmp(*argv, "--inbytes") == 0) { rawin = 2; }     // rawbytes input
        else {
            if (!rawin) fp = fopen(*argv, "rb");
            else        fp = fopen(*argv, "r");
            if (fp == NULL) {
                fprintf(stderr, "error open %s\n", *argv);
                return -1;
            }
            fileloaded = 1;
        }
        ++argv;
    }
    if (!fileloaded) fp = stdin;


    if (!rawin) {

        i = read_wav_header(fp);
        if (i) {
            fclose(fp);
            return -1;
        }

        for (pos = 0; pos < HEADLEN; pos++) {
            frame_rawbits[pos] = header[HEADOFS+pos];
        }

        while (!read_bits_fsk(fp, &bit, &len)) {

            if (len == 0) { // reset_frame();
            /*  if (pos > 2*BITS*pos_GPSV) {
                    print_bitframe(pos);
                    pos = HEADLEN;
                    header_found = 0;
                } */
                continue;   // ...
            }

            for (i = 0; i < len; i++) {

                inc_bufpos();
                buf[bufpos] = 0x30+bit;

                if (!header_found) {
                    header_found = compare();
                }
                else {
                    frame_rawbits[pos] = 0x30+bit;
                    //printf("%d", bit);
                    pos++;
                    if (pos == RAWBITFRAME_LEN) {
                        //frames++;
                        print_bitframe(pos);
                        header_found = 0;
                        pos = HEADLEN;
                    }
                }
            }
        }

    }
    else if (rawin==1) {  // input: bits
        while (1 > 0) {
            pbuf = fgets(frame_rawbits, RAWBITFRAME_LEN+4, fp);
            if (pbuf == NULL) break;
            frame_rawbits[RAWBITFRAME_LEN+1] = '\0';
            len = strlen(frame_rawbits);
            if (len > 2*BITS*pos_GPSpAcc) print_bitframe(len);
        }
    }
    else {  // input: bytes
        while (1 > 0) {              // rawin=2: 2chars->1byte
            pbuf = fgets(frame_rawbits, rawin*FRAME_LEN+4, fp);
            if (pbuf == NULL) break;
            frame_rawbits[rawin*FRAME_LEN+1] = '\0';
            len = strlen(frame_rawbits) / rawin;
            for (i = 0; i < len; i++) { //%2x  SCNx8=%hhx(inttypes.h)
                sscanf(frame_rawbits+rawin*i, "%2hhx", frame_bytes+i);
            }
            for (i = len; i < FRAME_LEN; i++) frame_bytes[i] = 0x00;
            if (frame_bytes[0] == 0xFC  &&  frame_bytes[1] == 0x1D) { // Header: 1A CF FC 1D
                print_frame();
            }
        }
    }

    fclose(fp);

    return 0;
}

