
/*
    UAII2022 Lindenberg
    Aerospace Newsky CF-06AH
    Huayuntianyi HT03G-1U
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ECC
//needs bch_ecc_mod.c, bch_ecc_mod.h
#include "bch_ecc_mod.c"
#endif

// optional JSON "version"
//  (a) set global
//      gcc -DVERSION_JSN [-I<inc_dir>] ...
#ifdef VERSION_JSN
  #include "version_jsn.h"
#endif
// or
//  (b) set local compiler option, e.g.
//      gcc -DVER_JSN_STR=\"0.0.2\" ...


typedef unsigned char ui8_t;
typedef short i16_t;
typedef unsigned int ui32_t;


static int option_verbose = 0,
           option_raw = 0,
           option_inv = 0,
           option_b = 0,
           option_ecc = 0,
           option_timestamp = 0,
           option_json = 0,
           wavloaded = 0;
static int wav_channel = 0;     // audio channel: left


#define BAUD_RATE   2400

#define FRAMELEN    109 //128
#define BITFRAMELEN (8*FRAMELEN)

#define HEADLEN 64
#define HEADOFS 0
static char header[] = "01010101""01010101""01010101""01010101""01010101"  // preamble AA AA AA
                       "10110100""00101011""11000110"; // 2D D4 63
static char buf[HEADLEN+1] = "xxxxxxxxxx\0";
static int bufpos = 0;

static char frame_bits[BITFRAMELEN+1];
static ui8_t frame_bytes[FRAMELEN+1];

typedef struct {
    int frnr;
    ui32_t sn;
    char id[8+4];
    //int week;
    int gpstow; // tow/ms
    //int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    float lat; float lon; float alt;
    float vH; float vD; float vV;
    //float vE; float vN; float vU;
    //char  frame_bits[BITFRAME_LEN+1];
    //ui8_t frame_bytes[FRAME_LEN+1];
    //int freq;
    int jsn_freq;   // freq/kHz (SDR)
    //option_t option;
} gpx_t;

static gpx_t gpx; // = {0}

#ifdef ECC
static ui8_t cw[255];
static ui8_t err_pos[255], err_val[255];
static int   errs1, errs2;

static RS_t RS_CF06 = { .N=255, .t=3, .R=6, .K=249, .b=1, .p=1, 1, {0}, {0} }; // RS(255,249), t=3, b=1, p=1, f=0x11D

static int parlen = 6;
static int msg1len = 42;
static int msg2len = 41;
#endif

static int sample_rate = 0, bits_sample = 0, channels = 0;
static float samples_per_bit = 0;

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static int read_wav_header(FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4) && strncmp(txt, "RF64", 4)) return -1;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4))  return -1;

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

    if (sample_rate == 900001) sample_rate -= 1;
    samples_per_bit = sample_rate/(float)BAUD_RATE;
    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    return 0;
}

static unsigned long sample_count = 0;
static double bitgrenze = 0;

static int f32read_sample(FILE *fp, float *s) {
    int i;
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;

    for (i = 0; i < channels; i++) {

        if (fread( &word, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == wav_channel) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (bits_sample == 32) {
                *s = *f;
            }
            else {
                if (bits_sample ==  8) { *b -= 128; }
                *s = *b/128.0;
                if (bits_sample == 16) { *s /= 256.0; }
            }
        }
    }

    sample_count++;

    return 0;
}

static int par=1, par_alt=1;

static int read_bits_fsk(FILE *fp, int *bit, int *len) {
    static float sample;
    int n;
    float l;

    n = 0;
    do {
        if ( f32read_sample(fp, &sample) == EOF ) return EOF;

        par_alt = par;
        par =  (sample >= 0.0f) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        n++;
    } while (par*par_alt > 0);

    l = (float)n / samples_per_bit;

    *len = (int)(l+0.5);

    if (!option_inv) *bit = (1+par_alt)/2;  // oben 1, unten -1
    else             *bit = (1-par_alt)/2;  // sdr#<rev1381?, invers: unten 1, oben -1
// *bit = (1+inv*par_alt)/2; // ausser inv=0

    /* Y-offset ? */

    return 0;
}

static int bitstart = 0;
static unsigned long scount = 0;
static int read_rawbit(FILE *fp, int *bit) {
    float sample;
    float sum;

    sum = 0.0f;

    if (bitstart) {
        scount = 0;    // eigentlich scount = 1
        bitgrenze = 0; //   oder bitgrenze = -1
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        if ( f32read_sample(fp, &sample) == EOF ) return EOF;
        //sample_count++; // in f32read_sample()
        //par =  (sample >= 0.0f) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0.0f) *bit = 1;
    else             *bit = 0;

    if (option_inv) *bit ^= 1;

    return 0;
}


static int compare() {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == header[HEADLEN+HEADOFS-1-i])) {
        i++;
    }
    return i;
}

static char inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

static int compare2() {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == inv(header[HEADLEN+HEADOFS-1-i]))) {
        i++;
    }
    return i;
}


static ui32_t crc16(ui8_t bytes[], int len) { // CF06AH
    ui32_t crc16poly = 0x1021;
    ui32_t rem = 0; // init value
    int i, j;
    for (i = 0; i < len; i++) {
        rem = rem ^ (bytes[i] << 8);
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

static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

#define OFS 5 // preamble

static int print_cf06() {
    int j;
    int crcdat1, crcval1, crc_ok1,
        crcdat2, crcval2, crc_ok2;

    if (option_ecc == 2) {  // always (frame_bytes+OFS)[55..64] = 0xAA ?
        for (j = 0; j < 10; j++) frame_bytes[OFS+55+j] = 0xAA;
    }

    #ifdef ECC
    if (option_ecc)
    {
        memset(cw, 0, 255);
        for (j = 0; j < msg1len; j++) cw[255-1-j] = frame_bytes[OFS+7+j];
        for (j = 0; j < parlen;  j++) cw[255-1-msg1len-j] = frame_bytes[OFS+7+msg1len+j];
        errs1 = rs_decode(&RS_CF06, cw, err_pos, err_val);
        if (errs1 > 0) {
            for (j = 0; j < msg1len; j++) frame_bytes[OFS+7+j] = cw[255-1-j];
            for (j = 0; j < parlen;  j++) frame_bytes[OFS+7+msg1len+j] = cw[255-1-msg1len-j];
        }

        memset(cw, 0, 255);
        for (j = 0; j < msg2len; j++) cw[255-1-j] = frame_bytes[OFS+55+j];
        for (j = 0; j < parlen;  j++) cw[255-1-msg2len-j] = frame_bytes[OFS+55+msg2len+j];
        errs2 = rs_decode(&RS_CF06, cw, err_pos, err_val);
        if (errs2 > 0) {
            for (j = 0; j < msg2len; j++) frame_bytes[OFS+55+j] = cw[255-1-j];
            for (j = 0; j < parlen;  j++) frame_bytes[OFS+55+msg2len+j] = cw[255-1-msg2len-j];
        }
    }
    #endif

    // CRC_1
    crcdat1 = (frame_bytes[OFS+47]<<8) | frame_bytes[OFS+47+1];
    crcval1 = crc16(frame_bytes+OFS+7, 40);
    crc_ok1 = (crcdat1 == crcval1);
    // CRC_2                                                  // (frame_bytes+OFS)[55..64] = 0xAA ?
    // int _crcval2 = crc16(frame_bytes+OFS+65, 29) ^ 0x39BB; // init: crc16(0xAA..AA), 10);
    // int _crcvalAA = crc16(frame_bytes+OFS+55, 10);         // crc16(0xAA..AA, 10) = 0x8a8f;
    // int _crcval3 = crc16_init8a8f(frame_bytes+OFS+65, 29); // init: 0x8a8f
    crcdat2 = (frame_bytes[OFS+94]<<8) | frame_bytes[OFS+94+1];
    crcval2 = crc16(frame_bytes+OFS+55, 39);
    crc_ok2 = (crcdat2 == crcval2);

    if (option_raw) {
        if (option_raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                printf("%02X ", frame_bytes[j]);
            }
            printf(" #  [%s,%s]", crc_ok1 ? "OK1" : "NO1", crc_ok2 ? "OK2" : "NO2");
            #ifdef ECC
            if (option_ecc && (errs1 || errs2)) printf("  (%d,%d)", errs1, errs2);
            #endif
        }
        else {
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", frame_bits[j]);
                if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    else {
        ui32_t val;
        int ival;
        ui32_t gpstime;
        ui32_t std, min, ms;
        int wday;
        float sek = 0.0f;

        ui32_t sn = 0;
        for (j = 0; j < 4; j++) {
            sn *= 100;
            sn += frame_bytes[OFS+7+j] % 100;
        }
        printf(" (%08d)  ", sn);
        gpx.sn = sn;
        sprintf(gpx.id, "%08d", sn);

        gpstime = 0;
        for (j = 0; j < 4; j++) gpstime |= frame_bytes[OFS+11+j] << (8*j);

        gpx.gpstow = gpstime;
        gpx.frnr = gpx.gpstow/1000; // JSON: 7-day wrap-around

        ms = gpstime % 1000;
        gpstime /= 1000;

        wday = (gpstime / (24 * 3600)) % 7;
        if ((wday < 0) || (wday > 6)) wday = 0;
        gpx.wday = wday;

        gpstime %= (24*3600);

        std =  gpstime / 3600;
        min = (gpstime % 3600) / 60;
        sek =  gpstime % 60 + ms/1000.0;
        gpx.std = std;
        gpx.min = min;
        gpx.sek = sek;

        printf("%s ", weekday[wday]);

        printf("%02d:%02d:%06.3f ", std, min, sek);
        printf(" ");

        val = 0;
        for (j = 0; j < 4; j++) val |= frame_bytes[OFS+15+j] << (8*j);
        ival = (int)val;
        float lon = ival / 1e7;
        printf(" lon: %.4f ", lon);
        gpx.lon = lon;

        val = 0;
        for (j = 0; j < 4; j++) val |= frame_bytes[OFS+19+j] << (8*j);
        ival = (int)val;
        float lat = ival / 1e7;
        printf(" lat: %.4f ", lat);
        gpx.lat = lat;

        val = 0;
        for (j = 0; j < 4; j++) val |= frame_bytes[OFS+23+j] << (8*j);
        ival = (int)val;
        float alt = ival / 1e3;
        printf(" alt: %.1f ", alt);  // MSL
        gpx.alt = alt;


        i16_t val16;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= frame_bytes[OFS+27+j] << (8*j);
        float vE = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= frame_bytes[OFS+29+j] << (8*j);
        float vN = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= frame_bytes[OFS+31+j] << (8*j);
        float vU = -val16/100.0f;
        //printf(" (%.2f,%.2f,%.2f) ", vE, vN, vU);
        float vH = sqrt(vN*vN+vE*vE);
        float vD = atan2(vE, vN) * 180.0 / M_PI;
        if (vD < 0) vD += 360;
        printf("  vH: %4.1f  D: %5.1f  vV: %3.1f ", vH, vD, vU);
        gpx.vH = vH;
        gpx.vD = vD;
        gpx.vV = vU;

        printf(" ");

        // 8-bit counter
        val = frame_bytes[OFS+45];
        printf("[%3d] ", val);

        printf(" ");

        // CRC_1
        printf(" %s", crc_ok1 ? "[OK1]" : "[NO1]");
        if (option_verbose) printf(" # [%04X:%04X]", crcdat1, crcval1);
        // CRC_2
        if (option_verbose) {
            printf(" ");
            printf(" %s", crc_ok2 ? "[OK2]" : "[NO2]");
            if (option_verbose) printf(" # [%04X:%04X]", crcdat2, crcval2);

            #ifdef ECC
            if (option_ecc && (errs1 || errs2)) printf("  (%d,%d)", errs1, errs2);
            #endif
        }

        printf("\n");
    }

    return crc_ok1; // crc_ok1 && crc_ok2
}

static ui32_t crc16rev(ui8_t bytes[], int len) { // HT03G
    ui32_t crc16poly = 0x8408; //rev(0x1021)
    ui32_t rem = 0; // init value
    int i, j;
    for (i = 0; i < len; i++) {
        rem = rem ^ bytes[i];
        for (j = 0; j < 8; j++) {
            if (rem & 0x0001) {
                rem = (rem >> 1) ^ crc16poly;
            }
            else {
                rem = (rem >> 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}

static int print_ht03() {
    int j;
    int crcdat, crcval, crc_ok;

    // CRC
    crcdat = frame_bytes[OFS+100] | (frame_bytes[OFS+100+1]<<8);
    crcval = crc16rev(frame_bytes+OFS+3, 97);
    crc_ok = (crcdat == crcval);

    if (option_raw) {
        if (option_raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                printf("%02X ", frame_bytes[j]);
            }
            printf(" #  %s", crc_ok ? "[OK]" : "[NO]");
        }
        else {
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", frame_bits[j]);
                if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    else {

        // SN/ID? DBG
        ////for (j=0;j<4;j++) printf("%02X", frame_bytes[OFS+7+j]); printf(" ");
        ui32_t sn = 0;
        for (j = 0; j < 4; j++) sn |= frame_bytes[OFS+7+j] << (8*(3-j));
        printf(" (%08X) ", sn);
        gpx.sn = sn;
        sprintf(gpx.id, "%08X", sn);

        printf(" ");

        ui32_t std, min;
        float sek = 0.0f;
        std = frame_bytes[OFS+12] & 0x1F;
        min = frame_bytes[OFS+13] & 0x3F;
        sek = (frame_bytes[OFS+14] | (frame_bytes[OFS+13] & 0xC0) << 2) / 10.0f;
        printf("%02d:%02d:%04.1f ", std, min, sek);  // UTC
        printf(" ");
        gpx.std = std;
        gpx.min = min;
        gpx.sek = sek;

        int val;  // GPS: little endian

        val = 0;
        for (j = 0; j < 4; j++) val |= frame_bytes[OFS+16+j] << (8*j);
        float *fval = (float*)(frame_bytes+OFS+16);
        float lon = *fval * 180.0 / M_PI;
        printf(" lon: %.4f ", lon);
        gpx.lon = lon;

        val = 0;
        for (j = 0; j < 4; j++) val |= frame_bytes[OFS+20+j] << (8*j);
        fval = (float*)(frame_bytes+OFS+20);
        float lat = *fval * 180.0 / M_PI;
        printf(" lat: %.4f ", lat);
        gpx.lat = lat;

        val = 0; // signed int32 or int24
        for (j = 0; j < 4; j++) val |= frame_bytes[OFS+24+j] << (8*j);
        float alt = val/10.0f;
        printf(" alt: %.1f ", alt);  // MSL
        gpx.alt = alt;

        i16_t val16;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= frame_bytes[OFS+28+j] << (8*j);
        float vN = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= frame_bytes[OFS+30+j] << (8*j);
        float vE = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= frame_bytes[OFS+32+j] << (8*j);
        float vU = val16/100.0f;
        //printf(" (%.2f,%.2f,%.2f) ", vN, vE, vU);
        float vH = sqrt(vN*vN+vE*vE);
        float vD = atan2(vE, vN) * 180.0 / M_PI;
        if (vD < 0) vD += 360;
        printf("  vH: %4.1f  D: %5.1f  vV: %3.1f ", vH, vD, vU);
        gpx.vH = vH;
        gpx.vD = vD;
        gpx.vV = vU;


        printf(" ");

        // counter ?  big endian
        int cnt = 0;
        for (j = 0; j < 2; j++) cnt |= frame_bytes[OFS+97+j] << (8*(1-j));
        printf(" [%5d] ", cnt);
        gpx.frnr = cnt;

        printf(" ");

        // CRC
        printf(" %s", crc_ok ? "[OK]" : "[NO]");
        if (option_verbose) printf(" # [%04X:%04X]", crcdat, crcval);

        printf("\n");
    }

    return crc_ok;
}

#define CF06 6
#define HT03 3

static int print_frame() {
    int frm_ok = 0;
    int rs_typ = 0;
    int i, j;
    char *rs_str = "";

    for (j = 0; j < FRAMELEN; j++) {
        ui8_t byteval = 0;
        ui8_t d = 1;
        for (i = 0; i < 8; i++) { /* little endian / LSB */
            if (frame_bits[8*j+i] & 1) byteval += d;
            d <<= 1;
        }
        frame_bytes[j] = byteval;
    }


    if  (frame_bytes[OFS+5] == 0xAA && frame_bytes[OFS+6] == 0xAA)  rs_typ = CF06;  // (2D D4 63 7F FF) AA AA
    else
    if  (frame_bytes[OFS+5] == 0xFF && frame_bytes[OFS+6] == 0xFF)  rs_typ = HT03;  // (2D D4 63 7F FF) FF EE
    else rs_typ = HT03;

    switch (rs_typ)
    {
        case CF06:  frm_ok = print_cf06();
                    rs_str = "CF06"; // CF-06-AH
                    break;
        case HT03:  frm_ok = print_ht03();
                    rs_str = "HT03"; // HT03G-1U
                    break;
        default:
                    break;
    }

    if (option_json && frm_ok && !option_raw) {
        char *ver_jsn = NULL;
        printf("{ \"type\": \"%s\"", rs_str);
        printf(", \"frame\": %d, \"id\": \"%.4s-%.8s\", \"datetime\": \"%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
               gpx.frnr, rs_str, gpx.id, gpx.std, gpx.min, gpx.sek, gpx.lat, gpx.lon, gpx.alt, gpx.vH, gpx.vD, gpx.vV );
        //printf(", \"subtype\": \"%s\"", rs_typ_str);
        if (gpx.jsn_freq > 0) {
            printf(", \"freq\": %d", gpx.jsn_freq);
        }

        // Reference time/position
        printf(", \"ref_datetime\": \"%s\"", (rs_typ == CF06) ? "GPS" : "UTC" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
        printf(", \"ref_position\": \"%s\"", "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

        #ifdef VER_JSN_STR
            ver_jsn = VER_JSN_STR;
        #endif
        if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
        printf(" }\n");
        printf("\n");
    }

    return 0;
}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;

    int j, h, bit, len;
    int bit_count, frames;
    int header_found = 0;

    int cfreq = -1;


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v\n");
            fprintf(stderr, "       -r\n");
            fprintf(stderr, "       -i\n");
            fprintf(stderr, "       -b\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if   (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        else if   (strcmp(*argv, "-t" ) == 0) { option_timestamp = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { option_ecc = 1; }
        else if   (strcmp(*argv, "--ecc2") == 0) { option_ecc = 2; }
        else if   (strcmp(*argv, "--json") == 0) {
            option_json = 1;
        }
        else if ( (strcmp(*argv, "--jsn_cfq") == 0) ) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
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


    j = read_wav_header(fp);
    if (j) return -1;

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;

    #ifdef ECC
    RS_CF06.GF = GF256RS; // f=0x11D
    rs_init_RS(&RS_CF06);
    #endif

    bit_count = 0;
    frames = 0;
    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) {
            bufpos--;
            if (bufpos < 0) bufpos = HEADLEN-1;
            buf[bufpos] = 'x';
            continue;
        }


        for (j = 0; j < len; j++) {
            bufpos--;
            if (bufpos < 0) bufpos = HEADLEN-1;
            buf[bufpos] = 0x30 + bit;

            if (!header_found)
            {
                h = compare(); //h2 = compare2();
                if ((h >= HEADLEN)) {
                    header_found = 1;
                    fflush(stdout);
                    if (option_timestamp) printf("<%8.3f> ", sample_count/(double)sample_rate);
                    strncpy(frame_bits, header, HEADLEN);
                    bit_count += HEADLEN;
                    frames++;
                }
            }
            else
            {
                frame_bits[bit_count] = 0x30 + bit;
                bit_count += 1;
            }

            if (bit_count >= BITFRAMELEN) {
                bit_count = 0;
                header_found = 0;

                print_frame();
            }

        }
        if (header_found && option_b) {
            bitstart = 1;

            while ( bit_count < BITFRAMELEN ) {
                if (read_rawbit(fp, &bit) == EOF) break;
                frame_bits[bit_count] = 0x30 + bit;
                bit_count += 1;
            }

            bit_count = 0;
            header_found = 0;

            print_frame();
        }
    }
    printf("\n");

    fclose(fp);

    return 0;
}

