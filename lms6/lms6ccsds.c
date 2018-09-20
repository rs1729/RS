
/*
   LMS6
   (403 MHz)

    gcc lms6ccsds.c -lm -o lms6ccsds
    ./lms6ccsds -b -v --vit --ecc <audio.wav>
*/

#include <stdio.h>
#include <string.h>
#include <math.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;

#include "bch_ecc.c"  // RS/ecc/


int option_verbose = 0,  // ausfuehrliche Anzeige
    option_b   = 0,
    option_raw = 0,      // rohe Frames
    option_ecc = 0,
    option_vit = 0,
    option_inv = 0,      // invertiert Signal
    option_res = 0,      // genauere Bitmessung
    wavloaded = 0;


/* -------------------------------------------------------------------------- */

#define BAUD_RATE   4800

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

unsigned long sample_count = 0;

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

    sample_count++;

    if (bits_sample ==  8) return ret-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16) return (short)ret;

    return ret;
}

int par=1, par_alt=1;

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    static int sample;
    int n, y0;
    float l, x1;
    static float x0;

    n = 0;
    do{
        y0 = sample;
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++;
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

double bitgrenze = 0;

int bitstart = 0;
int read_rawbit(FILE *fp, int *bit) {
    int sample;
    int n, sum;

    sum = 0;
    n = 0;

    if (bitstart) {
        n = 1;    // d.h. bitgrenze = sample_count-1 (?)
        bitgrenze = sample_count-1;
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        //par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;
        n++;
    } while (sample_count < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (option_inv) *bit ^= 1;

    return 0;
}

/* -------------------------------------------------------------------------- */


#define BITS 8
#define HEADOFS  16
#define HEADLEN ((4*16)-HEADOFS)
// RS-SYNC
//       (00)    58                f3                3f                b8
char header[] = "0000001101011101""0100100111000010""0100111111110010""0110100001101011";
ui8_t rs_sync[] = { 0x00, 0x58, 0xf3, 0x3f, 0xb8};
// 0x58f33fb8 little-endian <-> 0x1ACFFC1D big-endian bytes

#define SYNC_LEN 5
#define FRM_LEN    (223)
#define PAR_LEN    (32)
#define FRMBUF_LEN (3*FRM_LEN)
#define BLOCKSTART (SYNC_LEN*BITS*2)
#define BLOCK_LEN  (FRM_LEN+PAR_LEN+SYNC_LEN)  // 255+5 = 260
#define RAWBITBLOCK_LEN ((BLOCK_LEN+1)*BITS*2) // (+1 tail)

//                                                      (00)               58                f3                3f                b8
char  blk_rawbits[RAWBITBLOCK_LEN+SYNC_LEN*BITS*2 +8] = "0000000000000000""0000001101011101""0100100111000010""0100111111110010""0110100001101011";
//char  *block_rawbits = blk_rawbits+SYNC_LEN*BITS*2;

ui8_t block_bytes[BLOCK_LEN+8];


ui8_t frm_sync[] = { 0x24, 0x54, 0x00, 0x00};
ui8_t frame[FRM_LEN] = { 0x24, 0x54, 0x00, 0x00}; // dataheader

ui8_t *p_frame = frame;


#define FRAME_LEN       (300)  // 4800baud, 16bits/byte
#define BITFRAME_LEN    (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)
#define OVERLAP 64
#define OFS 4


char  frame_bits[BITFRAME_LEN+OVERLAP*BITS +8];  // init K-1 bits mit 0


char buf[HEADLEN];
int bufpos = -1;


#define K 7  // d_f=10
    char polyA[] = "1001111"; // 0x4f: x^6+x^3+x^2+x+1
    char polyB[] = "1101101"; // 0x6d: x^6+x^5+x^3+x^2+1
/*
// d_f=6
qA[] = "1110011";  // 0x73: x^6+x^5+x^4+x+1
qB[] = "0011110";  // 0x1e: x^4+x^3+x^2+x
pA[] = "10010101"; // 0x95: x^7+x^4+x^2+1 = (x+1)(x^6+x^5+x^4+x+1)        = (x+1)qA
pB[] = "00100010"; // 0x22: x^5+x         = (x+1)(x^4+x^3+x^2+x)=x(x+1)^3 = (x+1)qB
polyA = qA + x*qB
polyB = qA + qB
*/

char vit_rawbits[RAWBITFRAME_LEN+OVERLAP*BITS*2 +8];

#define N (1 << K)
#define M (1 << (K-1))

typedef struct {
    ui8_t bIn;
    ui8_t codeIn;
    int w;
    int prevState;
} states_t;

states_t vit_state[RAWBITFRAME_LEN+OVERLAP +8][M];

states_t vit_d[N];

ui8_t vit_code[N];


int vit_initCodes() {
    int cA, cB;
    int i, bits;

    for (bits = 0; bits < N; bits++) {
        cA = 0;
        cB = 0;
        for (i = 0; i < K; i++) {
            cA ^= (polyA[K-1-i]&1) & ((bits >> i)&1);
            cB ^= (polyB[K-1-i]&1) & ((bits >> i)&1);
        }
        vit_code[bits] = (cA<<1) | cB;
    }

    return 0;
}

int vit_dist(int c, char *rc) {
    return (((c>>1)^rc[0])&1) + ((c^rc[1])&1);
}

int vit_start(char *rc) {
    int t, m, j, c, d;

    t = K-1;
    m = M;
    while ( t > 0 ) {  // t=0..K-2: nextState<M
        for (j = 0; j < m; j++) {
            vit_state[t][j].prevState = j/2;
        }
        t--;
        m /= 2;
    }

    m = 2;
    for (t = 1; t < K; t++) {
        for (j = 0; j < m; j++) {
            c = vit_code[j];
            vit_state[t][j].bIn = j % 2;
            vit_state[t][j].codeIn = c;
            d = vit_dist( c, rc+2*(t-1) );
            vit_state[t][j].w = vit_state[t-1][vit_state[t][j].prevState].w + d;
        }
        m *= 2;
    }

    return t;
}

int vit_next(int t, char *rc) {
    int b, nstate;
    int j, index;

    for (j = 0; j < M; j++) {
        for (b = 0; b < 2; b++) {
            nstate = j*2 + b;
            vit_d[nstate].bIn = b;
            vit_d[nstate].codeIn = vit_code[nstate];
            vit_d[nstate].prevState = j;
            vit_d[nstate].w = vit_state[t][j].w + vit_dist( vit_d[nstate].codeIn, rc );
        }
     }

    for (j = 0; j < M; j++) {

        if ( vit_d[j].w <= vit_d[j+M].w ) index = j; else index = j+M;

        vit_state[t+1][j] = vit_d[index];
    }

    return 0;
}

int vit_path(int j, int t) {
    int c;

    vit_rawbits[2*t] = '\0';
    while (t > 0) {
        c = vit_state[t][j].codeIn;
        vit_rawbits[2*t -2] = 0x30 + ((c>>1) & 1);
        vit_rawbits[2*t -1] = 0x30 + (c & 1);
        j = vit_state[t][j].prevState;
        t--;
    }

    return 0;
}

int viterbi(char *rc) {
    int t, tmax;
    int j, j_min, w_min;

    vit_start(rc);

    tmax = strlen(rc)/2;

    for (t = K-1; t < tmax; t++)
    {
        vit_next(t, rc+2*t);
    }

    w_min = -1;
    for (j = 0; j < M; j++) {
        if (w_min < 0) {
            w_min = vit_state[tmax][j].w;
            j_min = j;
        }
        if (vit_state[tmax][j].w < w_min) {
            w_min = vit_state[tmax][j].w;
            j_min = j;
        }
    }
    vit_path(j_min, tmax);

    return 0;
}

// ------------------------------------------------------------------------

int deconv(char* rawbits, char *bits) {

    int j, n, bitA, bitB;
    char *p;
    int len;
    int errors = 0;
    int m = K-1;

    len = strlen(rawbits);
    for (j = 0; j < m; j++) bits[j] = '0';
    n = 0;
    while ( 2*(m+n) < len ) {
        p = rawbits+2*(m+n);
        bitA = bitB = 0;
        for (j = 0; j < m; j++) {
            bitA ^= (bits[n+j]&1) & (polyA[j]&1);
            bitB ^= (bits[n+j]&1) & (polyB[j]&1);
        }
        if      ( (bitA^(p[0]&1))==(polyA[m]&1)  &&  (bitB^(p[1]&1))==(polyB[m]&1) ) bits[n+m] = '1';
        else if ( (bitA^(p[0]&1))==0             &&  (bitB^(p[1]&1))==0            ) bits[n+m] = '0';
        else {
            if ( (bitA^(p[0]&1))!=(polyA[m]&1) && (bitB^(p[1]&1))==(polyB[m]&1) ) bits[n+m] = 0x39;
            else bits[n+m] = 0x38;
            errors = n;
            break;
        }
        n += 1;
    }
    bits[n+m] = '\0';

    return errors;
}

// ------------------------------------------------------------------------

int crc16_0(ui8_t frame[], int len) {
    int crc16poly = 0x1021;
    int rem = 0x0, i, j;
    int byte;

    for (i = 0; i < len; i++) {
        byte = frame[i];
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

int check_CRC(ui8_t frame[]) {
    ui32_t crclen = 0,
           crcdat = 0;

    crclen = 221;
    crcdat = (frame[crclen]<<8) | frame[crclen+1];
    if ( crcdat != crc16_0(frame, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
}

// ------------------------------------------------------------------------


void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

char cb_inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

int compare2() {
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

    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != cb_inv(header[HEADOFS+HEADLEN-1-i])) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return -1;

    return 0;

}

int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int len = strlen(bitstr)/8;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < len) {

        byteval = 0;
        d = 1;
        for (i = 0; i < BITS; i++) {
            bit=*(bitstr+bitpos+i);   /* little endian */
            //bit=*(bitstr+bitpos+7-i);  /* big endian */
            if        ((bit == '1') || (bit == '9'))    byteval += d;
            else /*if ((bit == '0') || (bit == '8'))*/  byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval & 0xFF;
    }

    //while (bytepos < FRAME_LEN+OVERLAP) bytes[bytepos++] = 0;

    return bytepos;
}

/* -------------------------------------------------------------------------- */

typedef struct {
    int frnr;
    int sn;
    int week; int gpstow;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double h;
    double vH; double vD; double vV;
    double vE; double vN; double vU;
    //int freq;
} gpx_t;

gpx_t gpx;

gpx_t gpx0 = { 0 };


#define pos_SondeSN  (OFS+0x00)  // ?4 byte 00 7A....
#define pos_FrameNb  (OFS+0x04)  // 2 byte
//GPS Position
#define pos_GPSTOW   (OFS+0x06)  // 4 byte
#define pos_GPSlat   (OFS+0x0E)  // 4 byte
#define pos_GPSlon   (OFS+0x12)  // 4 byte
#define pos_GPSalt   (OFS+0x16)  // 4 byte
//GPS Velocity East-North-Up (ENU)
#define pos_GPSvO    (OFS+0x1A)  // 3 byte
#define pos_GPSvN    (OFS+0x1D)  // 3 byte
#define pos_GPSvV    (OFS+0x20)  // 3 byte


int get_SondeSN() {
    unsigned byte;

    byte =  (p_frame[pos_SondeSN]<<24) | (p_frame[pos_SondeSN+1]<<16)
          | (p_frame[pos_SondeSN+2]<<8) | p_frame[pos_SondeSN+3];
    gpx.sn = byte & 0xFFFFFF;

    return 0;
}

int get_FrameNb() {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    gpx = gpx0;

    for (i = 0; i < 2; i++) {
        byte = p_frame[pos_FrameNb + i];
        frnr_bytes[i] = byte;
    }

    frnr = (frnr_bytes[0] << 8) + frnr_bytes[1] ;
    gpx.frnr = frnr;

    return 0;
}


char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
//char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

int get_GPStime() {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime = 0, // 32bit
        day;
    float ms;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSTOW + i];
        gpstime_bytes[i] = byte;
    }
    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpstime_bytes[i] << (8*(3-i));
    }

    gpx.gpstow = gpstime;

    ms = gpstime % 1000;
    gpstime /= 1000;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) return -1;

    gpx.wday = day;
    gpx.std = gpstime / 3600;
    gpx.min = (gpstime % 3600) / 60;
    gpx.sek = gpstime % 60 + ms/1000.0;

    return 0;
}

double B60B60 = 0xB60B60;  // 2^32/360 = 0xB60B60.xxx

int get_GPSlat() {
    int i;
    unsigned byte;
    ui8_t gpslat_bytes[4];
    int gpslat;
    double lat;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSlat + i];
        if (byte > 0xFF) return -1;
        gpslat_bytes[i] = byte;
    }

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8*(3-i));
    }
    lat = gpslat / B60B60;
    gpx.lat = lat;

    return 0;
}

int get_GPSlon() {
    int i;
    unsigned byte;
    ui8_t gpslon_bytes[4];
    int gpslon;
    double lon;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSlon + i];
        if (byte > 0xFF) return -1;
        gpslon_bytes[i] = byte;
    }

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8*(3-i));
    }
    lon = gpslon / B60B60;
    gpx.lon = lon;

    return 0;
}

int get_GPSalt() {
    int i;
    unsigned byte;
    ui8_t gpsheight_bytes[4];
    int gpsheight;
    double height;

    for (i = 0; i < 4; i++) {
        byte = p_frame[pos_GPSalt + i];
        if (byte > 0xFF) return -1;
        gpsheight_bytes[i] = byte;
    }

    gpsheight = 0;
    for (i = 0; i < 4; i++) {
        gpsheight |= gpsheight_bytes[i] << (8*(3-i));
    }
    height = gpsheight / 1000.0;
    gpx.h = height;

    if (height < -100 || height > 60000) return -1;
    return 0;
}

int get_GPSvel24() {
    int i;
    unsigned byte;
    ui8_t gpsVel_bytes[3];
    int vel24;
    double vx, vy, vz, dir; //, alpha;

    for (i = 0; i < 3; i++) {
        byte = p_frame[pos_GPSvO + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vx = vel24 / 1e3; // ost

    for (i = 0; i < 3; i++) {
        byte = p_frame[pos_GPSvN + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vy= vel24 / 1e3; // nord

    for (i = 0; i < 3; i++) {
        byte = p_frame[pos_GPSvV + i];
        if (byte > 0xFF) return -1;
        gpsVel_bytes[i] = byte;
    }
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vz = vel24 / 1e3; // hoch

    gpx.vE = vx;
    gpx.vN = vy;
    gpx.vU = vz;


    gpx.vH = sqrt(vx*vx+vy*vy);
/*
    alpha = atan2(vy, vx)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                  // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;         // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx.vD2 = dir;
*/
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx.vD = dir;

    gpx.vV = vz;

    return 0;
}


// RS(255,223)-CCSDS
#define rs_N 255
#define rs_K 223
#define rs_R (rs_N-rs_K) // 32
ui8_t rs_cw[rs_N];

int lms6_ecc(ui8_t *cw) {
    int errors;
    ui8_t err_pos[rs_R],
          err_val[rs_R];

    errors = rs_decode(cw, err_pos, err_val);

    return errors;
}

void print_frame(int crc_err, int len) {
    int err=0;

    if (p_frame[0] != 0)
    {
        //if ((p_frame[pos_SondeSN+1] & 0xF0) == 0x70)  // ? beginnen alle SNs mit 0x7A.... bzw 80..... ?
        if ( p_frame[pos_SondeSN+1] )
        {
            get_FrameNb();
            get_GPStime();
            get_SondeSN();
            if (option_verbose) printf(" (%7d) ", gpx.sn);
            printf(" [%5d] ", gpx.frnr);
            printf("%s ", weekday[gpx.wday]);
            printf("(%02d:%02d:%06.3f) ", gpx.std, gpx.min, gpx.sek); // falls Rundung auf 60s: Ueberlauf

            get_GPSlat();
            get_GPSlon();
            err = get_GPSalt();
            if (!err) {
                printf(" lat: %.6f° ", gpx.lat);
                printf(" lon: %.6f° ", gpx.lon);
                printf(" alt: %.2fm ", gpx.h);
                //if (option_verbose)
                {
                    get_GPSvel24();
                    //if (option_verbose == 2) printf("  (%.1f ,%.1f,%.1f) ", gpx.vE, gpx.vN, gpx.vU);
                    printf("  vH: %.1fm/s  D: %.1f°  vV: %.1fm/s ", gpx.vH, gpx.vD, gpx.vV);
                }
            }
            if (crc_err==0) printf(" [OK]"); else printf(" [NO]");

            printf("\n");
        }
    }
}

int blk_pos = SYNC_LEN;
int frm_pos = 0;
int sf = 0;

void proc_frame(int len) {

    char *rawbits = NULL;
    int i, j;
    int err = 0;
    int errs = 0;
    int crc_err = 0;
    int flen, blen;


    if ((len % 8) > 4) {
        while (len % 8) blk_rawbits[len++] = '0';
    }
    //if (len > RAWBITFRAME_LEN+OVERLAP*BITS*2) len = RAWBITFRAME_LEN+OVERLAP*BITS*2;
    //for (i = len; i < RAWBITFRAME_LEN+OVERLAP*BITS*2; i++) frame_rawbits[i] = 0;  // oder: '0'
    blk_rawbits[len] = '\0';

    flen = len / (2*BITS);

    if (option_vit) {
        viterbi(blk_rawbits);
        rawbits = vit_rawbits;
    }
    else rawbits = blk_rawbits;

    err = deconv(rawbits, frame_bits);

    if (err) { for (i=err; i < RAWBITBLOCK_LEN/2; i++) frame_bits[i] = 0; }


    blen = bits2bytes(frame_bits, block_bytes);
    for (j = blen; j < flen; j++) block_bytes[j] = 0;


    if (option_ecc) {
        for (j = 0; j < rs_N; j++) rs_cw[rs_N-1-j] = block_bytes[SYNC_LEN+j];
        errs = lms6_ecc(rs_cw);
        for (j = 0; j < rs_N; j++) block_bytes[SYNC_LEN+j] = rs_cw[rs_N-1-j];
    }

    if (option_raw == 2) {
        for (i = 0; i < flen; i++) printf("%02x ", block_bytes[i]);
        if (option_ecc) printf("(%d)", errs);
        printf("\n");
    }
    else if (option_raw == 4  &&  option_ecc) {
        for (i = 0; i < rs_N; i++) printf("%02x", block_bytes[SYNC_LEN+i]);
        printf(" (%d)", errs);
        printf("\n");
    }
    else if (option_raw == 8) {
        if (option_vit) {
            for (i = 0; i < len; i++) printf("%c", vit_rawbits[i]); printf("\n");
        }
        else {
            for (i = 0; i < len; i++) printf("%c", blk_rawbits[i]); printf("\n");
        }
    }

    blk_pos = SYNC_LEN;

    while ( blk_pos-SYNC_LEN < FRM_LEN ) {

        if (sf == 0) {
            while ( blk_pos-SYNC_LEN < FRM_LEN ) {
                sf = 0;
                for (j = 0; j < 4; j++) sf += (block_bytes[blk_pos+j] == frm_sync[j]);
                if (sf == 4)  {
                    frm_pos = 0;
                    break;
                }
                blk_pos++;
            }
        }

        if ( sf  &&  frm_pos < FRM_LEN ) {
            frame[frm_pos] = block_bytes[blk_pos];
            frm_pos++;
            blk_pos++;
        }

        if (frm_pos == FRM_LEN) {

            crc_err = check_CRC(p_frame);

            if (option_raw == 1) {
                for (i = 0; i < FRM_LEN; i++) printf("%02x ", p_frame[i]);
                if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
                printf("\n");
            }

            if (option_raw == 0) print_frame(crc_err, len);

            frm_pos = 0;
            sf = 0;
        }

    }

}


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int i, bit, len, rbit;
    int pos;
    int header_found = 0;
    unsigned int bc = 0;


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       --vit        (Viterbi)\n");
            fprintf(stderr, "       --ecc        (Reed-Solomon)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        //else if ( (strcmp(*argv, "-vv") == 0) ) option_verbose = 2;
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1; // bytes - rs_ecc_codewords
        }
        else if ( (strcmp(*argv, "-r0") == 0) || (strcmp(*argv, "--raw0") == 0) ) {
            option_raw = 2; // bytes: sync + codewords
        }
        else if ( (strcmp(*argv, "-rc") == 0) || (strcmp(*argv, "--rawecc") == 0) ) {
            option_raw = 4; // rs_ecc_codewords
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 8; // rawbits
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { option_ecc = 1; } // RS-ECC
        else if   (strcmp(*argv, "--vit" ) == 0) { option_vit = 1; } // viterbi-hard
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if   (strcmp(*argv, "--res") == 0) { option_res = 1; }
        else if   (strcmp(*argv, "-b") == 0) { option_b = 1; }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

    i = read_wav_header(fp);
    if (i) {
        fclose(fp);
        return -1;
    }


    if (option_raw == 4) option_ecc = 1;

    if (option_vit) {
        vit_initCodes();
    }
    if (option_ecc) {
        rs_init_RS255ccsds(); // bch_ecc.c
    }


    pos = BLOCKSTART;

    while (!read_bits_fsk(fp, &rbit, &len)) {

        if (len == 0) { // reset_frame();
            //inc_bufpos();
            //buf[bufpos] = 'x';
            //fprintf(stderr, "len==0\n");
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos();
            bit = rbit ^ (bc%2);  // (c0,inv(c1))
            bc++;
            buf[bufpos] = 0x30 + bit;

            if (!header_found) {
                header_found = compare2();
                if (header_found < 0) bc++;
            }
            else {
                if (pos < RAWBITBLOCK_LEN) {
                    blk_rawbits[pos] = 0x30 + bit;
                    pos++;
                }
            }

            if (pos >= RAWBITBLOCK_LEN) {
                    blk_rawbits[pos] = '\0';
                    proc_frame(pos);
                    pos = BLOCKSTART;
                    header_found = 0;
            }

        }
        if (header_found && option_b) {
            bitstart = 1;

            while ( pos < RAWBITBLOCK_LEN ) {
                if (read_rawbit(fp, &rbit) == EOF) break;
                bit = rbit ^ (bc%2);  // (c0,inv(c1))
                bc++;
                blk_rawbits[pos] = 0x30 + bit;
                pos++;
            }
            blk_rawbits[pos] = '\0';
            proc_frame(pos);
            pos = BLOCKSTART;
            header_found = 0;
        }

    }

    printf("\n");

    fclose(fp);

    return 0;
}

