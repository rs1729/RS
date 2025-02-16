
/*
 *  rs41
 *  sync header: correlation/matched filter
 *  compile, either (a) or (b):
 *  (a)
 *      gcc -DINCLUDESTATIC -c rs41base.c
 *  (b)
 *      gcc -c bch_ecc_mod.c
 *      gcc -c rs41base.c
 *
 *  author: zilog80
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif
*/

#include "demod_base.h"

//#define  INCLUDESTATIC 1
#ifdef INCLUDESTATIC
    #include "bch_ecc_mod.c"
#else
    #include "bch_ecc_mod.h"
#endif


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t crc;  // CRC check output
    i8_t ecc;  // Reed-Solomon ECC
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature humidity (pressure)
    i8_t dwp;  // PTU derived: dew point
    i8_t inv;
    i8_t aut;
    i8_t jsn;  // JSON output (auto_rx)
    i8_t slt;  // silent (only raw/json)
} option_t;

typedef struct {
    int typ;
    int msglen;
    int msgpos;
    int parpos;
    int hdrlen;
    int frmlen;
} rscfg_t;

static rscfg_t cfg_rs41 = { 41, (320-56)/2, 56, 8, 8, 320}; // const: msgpos, parpos


#define NDATA_LEN 320                    // std framelen 320
#define XDATA_LEN 198
#define FRAME_LEN (NDATA_LEN+XDATA_LEN)  // max framelen 518
/*
ui8_t //xframe[FRAME_LEN] = { 0x10, 0xB6, 0xCA, 0x11, 0x22, 0x96, 0x12, 0xF8},    = xorbyte( frame)
         frame[FRAME_LEN] = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60}; // = xorbyte(xframe)
*/

typedef struct {
    ui8_t id168;
    ui8_t status;
} gnss_t;

typedef struct {
    int out;
    int frnr;
    char id[9];
    ui8_t numSV;
    ui8_t gnss_numSVb168;
    ui8_t gnss_nSVstatus;
    gnss_t gnss_sv[32];
    ui8_t isUTC;
    int week; int tow_ms; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    float T; float RH; float TH;
    float P; float RH2;
    ui32_t crc;
    ui8_t frame[FRAME_LEN];
    ui8_t calibytes[51*16];
    ui8_t calfrchk[51];
    float ptu_Rf1;      // ref-resistor f1 (750 Ohm)
    float ptu_Rf2;      // ref-resistor f2 (1100 Ohm)
    float ptu_co1[3];   // { -243.911 , 0.187654 , 8.2e-06 }
    float ptu_calT1[3]; // calibration T1
    float ptu_co2[3];   // { -243.911 , 0.187654 , 8.2e-06 }
    float ptu_calT2[3]; // calibration T2-Hum
    float ptu_calH[2];  // calibration Hum
    float ptu_mtxH[42];
    float ptu_corHp[3];
    float ptu_corHt[12];
    float ptu_Cf1;
    float ptu_Cf2;
    float ptu_calP[25];
    ui32_t freq;    // freq/kHz (RS41)
    int jsn_freq;   // freq/kHz (SDR)
    float batt;     // battery voltage (V)
    ui16_t conf_fw; // firmware
    ui16_t conf_kt; // kill timer (sec)
    ui16_t conf_bt; // burst timer (sec)
    ui16_t conf_cd; // kill countdown (sec) (kt or bt)
    ui8_t  conf_bk; // burst kill
    char rstyp[9];  // RS41-SG, RS41-SGP
    char rsm[10];   // RSM421
    int  aux;
    char xdata[XDATA_LEN+16]; // xdata: aux_str1#aux_str2 ...
    option_t option;
    RS_t RS;
} gpx_t;


#define BITS    8
#define HEADLEN 64
#define FRAMESTART ((HEADLEN)/BITS)

/*                           10      B6      CA      11      22      96      12      F8      */
static char rs41_header[] = "0000100001101101010100111000100001000100011010010100100000011111";

static ui8_t rs41_header_bytes[8] = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60};

#define MASK_LEN 64
static ui8_t mask[MASK_LEN] = { 0x96, 0x83, 0x3E, 0x51, 0xB1, 0x49, 0x08, 0x98,
                                0x32, 0x05, 0x59, 0x0E, 0xF9, 0x44, 0xC6, 0x26,
                                0x21, 0x60, 0xC2, 0xEA, 0x79, 0x5D, 0x6D, 0xA1,
                                0x54, 0x69, 0x47, 0x0C, 0xDC, 0xE8, 0x5C, 0xF1,
                                0xF7, 0x76, 0x82, 0x7F, 0x07, 0x99, 0xA2, 0x2C,
                                0x93, 0x7C, 0x30, 0x63, 0xF5, 0x10, 0x2E, 0x61,
                                0xD0, 0xBC, 0xB4, 0xB6, 0x06, 0xAA, 0xF4, 0x23,
                                0x78, 0x6E, 0x3B, 0xAE, 0xBF, 0x7B, 0x4C, 0xC1};
/* LFSR: ab i=8 (mod 64):
 * m[16+i] = m[i] ^ m[i+2] ^ m[i+4] ^ m[i+6]
 * ________________3205590EF944C6262160C2EA795D6DA15469470CDCE85CF1
 * F776827F0799A22C937C3063F5102E61D0BCB4B606AAF423786E3BAEBF7B4CC196833E51B1490898
 */
/*
    frame[pos] = xframe[pos] ^ mask[pos % MASK_LEN];
*/

/* ------------------------------------------------------------------------------------ */

#define BAUD_RATE 4800

/* ------------------------------------------------------------------------------------ */
/*
 * Convert GPS Week and Seconds to Modified Julian Day.
 * - Adapted from sci.astro FAQ.
 * - Ignores UTC leap seconds.
 */
// in : week, gpssec
// out: jahr, monat, tag
static void Gps2Date(gpx_t *gpx) {
    long GpsDays, Mjd;
    long J, C, Y, M;

    GpsDays = gpx->week * 7 + (gpx->gpssec / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    gpx->tag = J - 2447 * M / 80;
    J = M / 11;
    gpx->monat = M + 2 - (12 * J);
    gpx->jahr = 100 * (C - 49) + Y + J;
}
/* ------------------------------------------------------------------------------------ */

static int bits2byte(char bits[]) {
    int i, byteval=0, d=1;
    for (i = 0; i < 8; i++) {     // little endian
    /* for (i = 7; i >= 0; i--) { // big endian */
        if      (bits[i] == 1)  byteval += d;
        else if (bits[i] == 0)  byteval += 0;
        else return 0x100;
        d <<= 1;
    }
    return byteval;
}

/* ------------------------------------------------------------------------------------ */

static ui32_t u4(ui8_t *bytes) {  // 32bit unsigned int
    ui32_t val = 0;
    memcpy(&val, bytes, 4);
    return val;
}

static ui32_t u3(ui8_t *bytes) {  // 24bit unsigned int
    int val24 = 0;
    val24 = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    // = memcpy(&val, bytes, 3), val &= 0x00FFFFFF;
    return val24;
}

static int i3(ui8_t *bytes) {  // 24bit signed int
    int val = 0,
        val24 = 0;
    val = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    val24 = val & 0xFFFFFF; if (val24 & 0x800000) val24 = val24 - 0x1000000;
    return val24;
}

static ui32_t u2(ui8_t *bytes) {  // 16bit unsigned int
    return  bytes[0] | (bytes[1]<<8);
}

static int i2(ui8_t *bytes) { // 16bit signed int
    //return (i16_t)u2(bytes);
    int val = bytes[0] | (bytes[1]<<8);
    if (val & 0x8000) val -= 0x10000;
    return val;
}

/*
double r8(ui8_t *bytes) {
    double val = 0;
    memcpy(&val, bytes, 8);
    return val;
}

float r4(ui8_t *bytes) {
    float val = 0;
    memcpy(&val, bytes, 4);
    return val;
}
*/

static int crc16(ui8_t data[], int len) {
    int crc16poly = 0x1021;
    int rem = 0xFFFF, i, j;
    int byte;

    //if (start+len+2 > FRAME_LEN) return -1;

    for (i = 0; i < len; i++) {
        byte = data[i];
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

static int check_CRC(gpx_t *gpx, ui32_t pos, ui32_t pck) {
    ui32_t crclen = 0,
           crcdat = 0;
    // check only pck_type (variable len pcks 0x76, 0x7E)
    if (((pck>>8) & 0xFF) != gpx->frame[pos]) return -1;
    crclen = gpx->frame[pos+1];
    if (pos + crclen + 4 > FRAME_LEN) return -1;
    crcdat = u2(gpx->frame+pos+2+crclen);
    if ( crcdat != crc16(gpx->frame+pos+2, crclen) ) {
        return 1;  // CRC NO
    }
    else return 0; // CRC OK
}


/*
GPS chip: ublox UBX-G6010-ST

  Pos: SubHeader, 1+1 byte (ID+LEN)
0x039: 7928   FrameNumber+SondeID
              +(0x050: 0732  CalFrames 0x00..0x32)
0x065: 7A2A   PTU
0x093: 7C1E   GPS1: RXM-RAW (0x02 0x10) Week, TOW, Sats
0x0B5: 7D59   GPS2: RXM-RAW (0x02 0x10) pseudorange, doppler
0x112: 7B15   GPS3: NAV-SOL (0x01 0x06) ECEF-POS, ECEF-VEL
0x12B: 7611   00
0x12B: 7Exx   AUX-xdata
*/

#define crc_FRAME      (1<<0)
#define xor_FRAME      0x1713  // ^0x6E3B=0x7928
#define pck_FRAME      0x7928
#define pos_FRAME       0x039
#define pos_FrameNb     0x03B  // 2 byte
#define pos_BattVolts   0x045  // 2 byte
#define pos_SondeID     0x03D  // 8 byte
#define pos_CalData     0x052  // 1 byte, counter 0x00..0x32
#define pos_Calfreq     0x055  // 2 byte, calfr 0x00
#define pos_Calburst    0x05E  // 1 byte, calfr 0x02
// ? #define pos_Caltimer  0x05A  // 2 byte, calfr 0x02 ?
#define pos_CalRSTyp    0x05B  // 8 byte, calfr 0x21 (+2 byte in 0x22?)
        // weitere chars in calfr 0x22/0x23; weitere ID (RSM)
#define pos_CalRSM      0x055  // 6 byte, calfr 0x22

#define crc_PTU        (1<<1)
#define xor_PTU        0xE388  // ^0x99A2=0x0x7A2A
#define pck_PTU        0x7A2A  // PTU
#define pos_PTU         0x065

#define crc_GPS1       (1<<2)
#define xor_GPS1       0x9667  // ^0xEA79=0x7C1E
#define pck_GPS1       0x7C1E  // RXM-RAW (0x02 0x10)
#define pos_GPS1        0x093
#define pos_GPSweek     0x095  // 2 byte
#define pos_GPSiTOW     0x097  // 4 byte
#define pos_satsN       0x09B  // 12x2 byte (1: SV, 1: quality,strength)

#define crc_GPS2       (1<<3)
#define xor_GPS2       0xD7AD  // ^0xAAF4=0x7D59
#define pck_GPS2       0x7D59  // RXM-RAW (0x02 0x10)
#define pos_GPS2        0x0B5
#define pos_minPR       0x0B7  //        4 byte
#define pos_FF          0x0BB  //        1 byte
#define pos_dataSats    0x0BC  // 12x(4+3) byte (4: pseudorange, 3: doppler)

#define crc_GPS3       (1<<4)
#define xor_GPS3       0xB9FF  // ^0xC2EA=0x7B15
#define pck_GPS3       0x7B15  // NAV-SOL (0x01 0x06)
#define pos_GPS3        0x112
#define pos_GPSecefX    0x114  //   4 byte
#define pos_GPSecefY    0x118  //   4 byte
#define pos_GPSecefZ    0x11C  //   4 byte
#define pos_GPSecefV    0x120  // 3*2 byte
#define pos_numSats     0x126  //   1 byte
#define pos_sAcc        0x127  //   1 byte
#define pos_pDOP        0x128  //   1 byte

#define crc_AUX        (1<<5)
#define pck_AUX        0x7E00  // LEN variable
#define pos_AUX         0x12B

#define crc_ZERO       (1<<6)  // LEN variable
#define pck_ZERO       0x7600
#define pck_ZEROstd    0x7611  // NDATA std-frm, no aux
#define pos_ZEROstd     0x12B  // pos_AUX(0)

#define pck_SGM_xTU    0x7F1B  // temperature/humidity
#define pck_SGM_CRYPT  0x80A7  // Packet type for an Encrypted payload

// fw 0x50dd
#define pck_960A              0x960A  //
#define pck_8226_POSDATETIME  0x8226  // ECEF-POS/VEL , DATE/TIME
#define pck_8329_SATS         0x8329  // GNSS sats


/*
  frame[pos_FRAME-1] == 0x0F: len == NDATA_LEN(320)
  frame[pos_FRAME-1] == 0xF0: len == FRAME_LEN(518)
*/
static int frametype(gpx_t *gpx) { // -4..+4: 0xF0 -> -4 , 0x0F -> +4
    int i;
    ui8_t b = gpx->frame[pos_FRAME-1];
    int ft = 0;
    for (i = 0; i < 4; i++) {
        ft += ((b>>i)&1) - ((b>>(i+4))&1);
    }
    return ft;
}

static int get_FrameNb(gpx_t *gpx, int crc, int ofs) {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_FrameNb+ofs + i];
        frnr_bytes[i] = byte;
    }

    frnr = frnr_bytes[0] + (frnr_bytes[1] << 8);
    gpx->frnr = frnr;

    return 0;
}

static int get_BattVolts(gpx_t *gpx, int ofs) {
    int i;
    unsigned byte;
    ui8_t batt_bytes[2];
    ui16_t batt_volts; // signed voltage?

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_BattVolts+ofs + i];
        batt_bytes[i] = byte;
    }
                                // 2 bytes? V > 25.5 ?
    batt_volts = batt_bytes[0]; // + (batt_bytes[1] << 8);
    gpx->batt = batt_volts/10.0;

    return 0;
}

static int get_SondeID(gpx_t *gpx, int crc, int ofs) {
    int i;
    unsigned byte;
    char sondeid_bytes[9];

    if (crc == 0) {
        for (i = 0; i < 8; i++) {
            byte = gpx->frame[pos_SondeID+ofs + i];
            //if ((byte < 0x20) || (byte > 0x7E)) return -1;
            sondeid_bytes[i] = byte;
        }
        sondeid_bytes[8] = '\0';
        if ( strncmp(gpx->id, sondeid_bytes, 8) != 0 ) {
            //for (i = 0; i < 51; i++) gpx->calfrchk[i] = 0;
            memset(gpx->calfrchk, 0, 51); // 0x00..0x32
            // reset conf data
            memset(gpx->rstyp, 0, 9);
            memset(gpx->rsm, 0, 10);
            gpx->freq = 0;
            gpx->conf_fw = 0;
            gpx->conf_bt = 0;
            gpx->conf_bk = 0;
            gpx->conf_cd = -1;
            gpx->conf_kt = -1;
            // don't reset gpx->frame[] !
            gpx->jahr = 0; gpx->monat = 0; gpx->tag = 0;
            gpx->std = 0; gpx->min = 0; gpx->sek = 0.0;
            gpx->week = 0;
            gpx->lat = 0.0; gpx->lon = 0.0; gpx->alt = 0.0;
            gpx->vH  = 0.0; gpx->vD  = 0.0; gpx->vV  = 0.0;
            gpx->numSV = 0;
            gpx->gnss_numSVb168 = 0;
            gpx->gnss_nSVstatus = 0;
            memset(gpx->gnss_sv, 0, 32*sizeof(gnss_t)); // gpx->gnss_sv[i].id168 = 0; gpx->gnss_sv[i].status = 0;
            gpx->isUTC = 0;
            gpx->T = -273.15f;
            gpx->RH = -1.0f;
            gpx->P = -1.0f;
            gpx->RH2 = -1.0f;
            // new ID:
            memcpy(gpx->id, sondeid_bytes, 8);
            gpx->id[8] = '\0';
        }
    }

    return 0;
}

static int get_FrameConf(gpx_t *gpx, int ofs) {
    int crc, err;
    ui8_t calfr;
    int i;

    crc = check_CRC(gpx, pos_FRAME+ofs, pck_FRAME);
    if (crc) gpx->crc |= crc_FRAME;

    err = crc;
    err |= get_SondeID(gpx, crc, ofs);
    err |= get_FrameNb(gpx, crc, ofs);
    err |= get_BattVolts(gpx, ofs);

    if (crc == 0) {
        calfr = gpx->frame[pos_CalData+ofs];
        if (gpx->calfrchk[calfr] == 0) // const?
        {                              // 0x32 not constant
            for (i = 0; i < 16; i++) {
                gpx->calibytes[calfr*16 + i] = gpx->frame[pos_CalData+ofs+1+i];
            }
            gpx->calfrchk[calfr] = 1;
        }
    }

    return err;
}

static int get_CalData(gpx_t *gpx) {

    int j;

    memcpy(&(gpx->ptu_Rf1), gpx->calibytes+61, 4);  // 0x03*0x10+13
    memcpy(&(gpx->ptu_Rf2), gpx->calibytes+65, 4);  // 0x04*0x10+ 1

    memcpy(gpx->ptu_co1+0, gpx->calibytes+77, 4);  // 0x04*0x10+13
    memcpy(gpx->ptu_co1+1, gpx->calibytes+81, 4);  // 0x05*0x10+ 1
    memcpy(gpx->ptu_co1+2, gpx->calibytes+85, 4);  // 0x05*0x10+ 5

    memcpy(gpx->ptu_calT1+0, gpx->calibytes+89, 4);  // 0x05*0x10+ 9
    memcpy(gpx->ptu_calT1+1, gpx->calibytes+93, 4);  // 0x05*0x10+13
    memcpy(gpx->ptu_calT1+2, gpx->calibytes+97, 4);  // 0x06*0x10+ 1
    // ptu_calT1[3..6]

    memcpy(gpx->ptu_calH+0, gpx->calibytes+117, 4);  // 0x07*0x10+ 5
    memcpy(gpx->ptu_calH+1, gpx->calibytes+121, 4);  // 0x07*0x10+ 9

    memcpy(gpx->ptu_co2+0, gpx->calibytes+293, 4);  // 0x12*0x10+ 5
    memcpy(gpx->ptu_co2+1, gpx->calibytes+297, 4);  // 0x12*0x10+ 9
    memcpy(gpx->ptu_co2+2, gpx->calibytes+301, 4);  // 0x12*0x10+13

    memcpy(gpx->ptu_calT2+0, gpx->calibytes+305, 4);  // 0x13*0x10+ 1
    memcpy(gpx->ptu_calT2+1, gpx->calibytes+309, 4);  // 0x13*0x10+ 5
    memcpy(gpx->ptu_calT2+2, gpx->calibytes+313, 4);  // 0x13*0x10+ 9
    // ptu_calT2[3..6]


    // cf. DF9DQ
    memcpy(&(gpx->ptu_Cf1), gpx->calibytes+69, 4);  // 0x04*0x10+ 5
    memcpy(&(gpx->ptu_Cf2), gpx->calibytes+73, 4);  // 0x04*0x10+ 9
    for (j = 0; j < 42; j++) {  // 0x07*0x10+13 = 0x07D = 125
        memcpy(gpx->ptu_mtxH+j, gpx->calibytes+125+4*j, 4);
    }
    for (j = 0; j <  3; j++) {  // 0x2A*0x10+6 = 0x2A6 = 678
        memcpy(gpx->ptu_corHp+j, gpx->calibytes+678+4*j, 4);
    }
    for (j = 0; j < 12; j++) {  // 0x2B*0x10+A = 0x2BA = 698
        memcpy(gpx->ptu_corHt+j, gpx->calibytes+698+4*j, 4);
    }
    // cf. DF9DQ or stsst/RS-fork
    memcpy(gpx->ptu_calP+0,  gpx->calibytes+606, 4); // 0x25*0x10+14 = 0x25E
    memcpy(gpx->ptu_calP+4,  gpx->calibytes+610, 4); // ..
    memcpy(gpx->ptu_calP+8,  gpx->calibytes+614, 4);
    memcpy(gpx->ptu_calP+12, gpx->calibytes+618, 4);
    memcpy(gpx->ptu_calP+16, gpx->calibytes+622, 4);
    memcpy(gpx->ptu_calP+20, gpx->calibytes+626, 4);
    memcpy(gpx->ptu_calP+24, gpx->calibytes+630, 4);
    memcpy(gpx->ptu_calP+1,  gpx->calibytes+634, 4);
    memcpy(gpx->ptu_calP+5,  gpx->calibytes+638, 4);
    memcpy(gpx->ptu_calP+9,  gpx->calibytes+642, 4);
    memcpy(gpx->ptu_calP+13, gpx->calibytes+646, 4);
    memcpy(gpx->ptu_calP+2,  gpx->calibytes+650, 4);
    memcpy(gpx->ptu_calP+6,  gpx->calibytes+654, 4);
    memcpy(gpx->ptu_calP+10, gpx->calibytes+658, 4);
    memcpy(gpx->ptu_calP+14, gpx->calibytes+662, 4);
    memcpy(gpx->ptu_calP+3,  gpx->calibytes+666, 4);
    memcpy(gpx->ptu_calP+7,  gpx->calibytes+670, 4); // ..
    memcpy(gpx->ptu_calP+11, gpx->calibytes+674, 4); // 0x2A*0x10+ 2

    return 0;
}

// temperature, platinum resistor
// T-sensor:    gpx->ptu_co1 , gpx->ptu_calT1
// T_RH-sensor: gpx->ptu_co2 , gpx->ptu_calT2
static float get_T(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, float *ptu_co, float *ptu_calT) {
    float *p = ptu_co;
    float *c = ptu_calT;
    float  g = (float)(f2-f1)/(gpx->ptu_Rf2-gpx->ptu_Rf1),       // gain
          Rb = (f1*gpx->ptu_Rf2-f2*gpx->ptu_Rf1)/(float)(f2-f1), // ofs
          Rc = f/g - Rb,
          R = Rc * c[0],
          T = (p[0] + p[1]*R + p[2]*R*R + c[1])*(1.0 + c[2]);
    return T; // [Celsius]
}

// rel.hum., capacitor
// (data:) ftp://ftp-cdc.dwd.de/climate_environment/CDC/observations_germany/radiosondes/
// (diffAlt: Ellipsoid-Geoid)
// (note: humidity sensor has significant time-lag at low temperatures)
static float get_RHemp(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, float T) {
    float a0 = 7.5;                    // empirical
    float a1 = 350.0/gpx->ptu_calH[0]; // empirical
    float fh = (f-f1)/(float)(f2-f1);
    float rh = 100.0 * ( a1*fh - a0 );
    float T0 = 0.0, T1 = -20.0, T2 = -40.0; // T/C    v0.4
    rh += T0 - T/5.5;                       // empir. temperature compensation
    if (T < T1) rh *= 1.0 + (T1-T)/100.0;   // empir. temperature compensation
    if (T < T2) rh *= 1.0 + (T2-T)/120.0;   // empir. temperature compensation
    if (rh < 0.0) rh = 0.0;
    if (rh > 100.0) rh = 100.0;
    if (T < -273.0) rh = -1.0;
    return rh;
}

// ---------------------------------------------------------------------------------------
//
// cf. github DF9DQ
// water vapor saturation pressure (Hyland and Wexler)
static float vaporSatP(float Tc) {
    double T = Tc + 273.15;

    // Apply some correction magic
    // T = -0.4931358 + (1.0 + 4.61e-3) * T - 1.3746454e-5 * T*T + 1.2743214e-8 * T*T*T;

    // H+W equation
    double p = expf(-5800.2206 / T
                    +1.3914993
                    +6.5459673 * log(T)
                    -4.8640239e-2 * T
                    +4.1764768e-5 * T*T
                    -1.4452093e-8 * T*T*T
                   );

    return (float)p; // [Pa]
}
// cf. github DF9DQ
static float get_RH2adv(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, float T, float TH, float P) {
    float rh  = 0.0;
    float cfh = (f-f1)/(float)(f2-f1);
    float cap = gpx->ptu_Cf1+(gpx->ptu_Cf2-gpx->ptu_Cf1)*cfh;
    double Cp = (cap / gpx->ptu_calH[0] - 1.0) * gpx->ptu_calH[1];
    double Trh_20_180 = (TH - 20.0) / 180.0;
    double _rh = 0.0;
    double aj = 1.0;
    double bk = 1.0, b[6];
    int j, k;

    bk = 1.0;
    for (k = 0; k < 6; k++) {
        b[k] = bk;  // Tp^k
        bk *= Trh_20_180;
    }

    if (P > 0.0) // in particular if P<200hPa , T<-40
    {
        double _p = P / 1000.0; // bar
        double _cpj = 1.0;
        double corrCp = 0.0;
        double bt, bp[3];

        for (j = 0; j < 3; j++) {
            bp[j] = gpx->ptu_corHp[j] * (_p/(1.0 + gpx->ptu_corHp[j]*_p) - _cpj/(1.0 + gpx->ptu_corHp[j]));
            _cpj *= Cp; // Cp^j
        }

        corrCp = 0.0;
        for (j = 0; j < 3; j++) {
            bt = 0.0;
            for (k = 0; k < 4; k++) {
                bt += gpx->ptu_corHt[4*j+k] * b[k];
            }
            corrCp += bp[j] * bt;
        }
        Cp -= corrCp;
    }

    aj = 1.0;
    for (j = 0; j < 7; j++) {
        for (k = 0; k < 6; k++) {
            _rh += aj * b[k] * gpx->ptu_mtxH[6*j+k];
        }
        aj *= Cp;
    }

    if ( P <= 0.0 ) {   // empirical correction
        float T2 = -40;
        if (T < T2) { _rh += (T-T2)/12.0; }
    }

    rh = _rh * vaporSatP(TH)/vaporSatP(T);

    if (rh < 0.0) rh = 0.0;
    if (rh > 100.0) rh = 100.0;
    return rh;
}
//
// cf. github DF9DQ or stsst/RS-fork
static float get_P(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, int fx)
{
    double p = 0.0;
    double a0, a1, a0j, a1k;
    int j, k;
    if (f1 == f2 || f1 == f) return 0.0;
    a0 = gpx->ptu_calP[24] / ((float)(f - f1) / (float)(f2 - f1));
    a1 = fx * 0.01;

    a0j = 1.0;
    for (j = 0; j < 6; j++) {
        a1k = 1.0;
        for (k = 0; k < 4; k++) {
            p += a0j * a1k * gpx->ptu_calP[j*4+k];
            a1k *= a1;
        }
        a0j *= a0;
    }

    return (float)p;
}
// ---------------------------------------------------------------------------------------
//
// barometric formula https://en.wikipedia.org/wiki/Barometric_formula
static float Ph(float h) {
    double Pb, Tb, Lb, hb;
	//double RgM = 8.31446/(9.80665*0.0289644);
	double gMR = 9.80665*0.0289644/8.31446;
	float P = 0.0;

    if (h > 32000.0) { //P < 8.6802
        Pb = 8.6802;
        Tb = 228.65;
        Lb = 0.0028;
        hb = 32000.0;
    }
    else if (h > 20000.0) { // P < 54.7489 (&& P >= 8.6802)
        Pb = 54.7489;
        Tb = 216.65;
        Lb = 0.001;
        hb = 20000.0;
    }
    else if (h > 11000.0) { // P < 226.321 (&& P >= 54.7489)
        Pb = 226.321;
        Tb = 216.65;
        Lb = 0.0;
        hb = 11000.0;
    }
    else {                 // P >= 226.321
        Pb = 1013.25;
        Tb = 288.15;
        Lb = -0.0065;
        hb = 0.0;
    }

    //if (Lb == 0.0) altP = -RgM*Tb * log(P/Pb) + hb;
    //else           altP = Tb/Lb * (pow(P/Pb, -RgM*Lb)-1.0) + hb;
    if (Lb == 0.0) P = Pb * exp( -gMR*(h-hb)/Tb );
    else           P = Pb * pow( 1.0+Lb*(h-hb)/Tb , -gMR/Lb);

    return P;
}

static int get_PTU(gpx_t *gpx, int ofs, int pck, int valid_alt) {
    int err=0, i;
    int bR, bc1, bT1,
            bc2, bT2;
    int bH;
    int bH2;
    int bP;
    ui32_t meas[12];
    float Tc = -273.15;
    float TH = -273.15;
    float RH = -1.0;
    float RH2 = -1.0;
    float P = -1.0;

    get_CalData(gpx);

    err = check_CRC(gpx, pos_PTU+ofs, pck);
    if (err) gpx->crc |= crc_PTU;

    if (err == 0)
    {
        // 0x7A2A: 16 byte (P)TU
        // 0x7F1B: 12 byte _TU
        for (i = 0; i < 12; i++) {
            meas[i] = u3(gpx->frame+pos_PTU+ofs+2+3*i);
        }

        bR  = gpx->calfrchk[0x03] && gpx->calfrchk[0x04];
        bc1 = gpx->calfrchk[0x04] && gpx->calfrchk[0x05];
        bT1 = gpx->calfrchk[0x05] && gpx->calfrchk[0x06];
        bc2 = gpx->calfrchk[0x12] && gpx->calfrchk[0x13];
        bT2 = gpx->calfrchk[0x13];
        bH  = gpx->calfrchk[0x07];

        bH2 = gpx->calfrchk[0x07] && gpx->calfrchk[0x08]
           && gpx->calfrchk[0x09] && gpx->calfrchk[0x0A]
           && gpx->calfrchk[0x0B] && gpx->calfrchk[0x0C]
           && gpx->calfrchk[0x0D] && gpx->calfrchk[0x0E]
           && gpx->calfrchk[0x0F] && gpx->calfrchk[0x10]
           && gpx->calfrchk[0x11] && gpx->calfrchk[0x12]
           && gpx->calfrchk[0x2A] && gpx->calfrchk[0x2B]
           && gpx->calfrchk[0x2C] && gpx->calfrchk[0x2D]
           && gpx->calfrchk[0x2E];

        bP  = gpx->calfrchk[0x21] && gpx->calibytes[0x21F] == 'P'
           && gpx->calfrchk[0x25] && gpx->calfrchk[0x26]
           && gpx->calfrchk[0x27] && gpx->calfrchk[0x28]
           && gpx->calfrchk[0x29] && gpx->calfrchk[0x2A];

        if (bR && bc1 && bT1) {
            Tc = get_T(gpx, meas[0], meas[1], meas[2], gpx->ptu_co1, gpx->ptu_calT1);
        }
        gpx->T = Tc;

        if (bR && bc2 && bT2) {
            TH = get_T(gpx, meas[6], meas[7], meas[8], gpx->ptu_co2, gpx->ptu_calT2);
        }
        gpx->TH = TH;

        if (bH && Tc > -273.0) {
            RH = get_RHemp(gpx, meas[3], meas[4], meas[5], Tc); // TH, TH-Tc (sensorT - T)
        }
        gpx->RH = RH;

        // cf. DF9DQ, stsst/RS-fork
        if (bP) {
            P = get_P(gpx, meas[9], meas[10], meas[11], i2(gpx->frame+pos_PTU+ofs+2+38));
        }
        gpx->P = P;
        if (gpx->option.ptu == 2) {
            float _P = -1.0;
            if (bP) _P = P;
            else { // approx
                if (valid_alt > 0) _P = Ph(gpx->alt);
            }
            if (bH && bH2 && Tc > -273.0 && TH > -273.0) {
                RH2 = get_RH2adv(gpx, meas[3], meas[4], meas[5], Tc, TH, _P);
            }
        }
        gpx->RH2 = RH2;


        if (gpx->option.vbs == 4 && (gpx->crc & (crc_PTU | crc_GPS3))==0)
        {
            printf("  h: %8.2f   # ", gpx->alt); // crc_GPS3 ?

            printf("1: %8d %8d %8d", meas[0], meas[1], meas[2]);
            printf("   #   ");
            printf("2: %8d %8d %8d", meas[3], meas[4], meas[5]);
            printf("   #   ");
            printf("3: %8d %8d %8d", meas[6], meas[7], meas[8]);
            printf("   #   ");

            if (0 && Tc > -273.0 && RH > -0.5)
            {
                printf("  ");
                printf(" Tc:%.2f ", Tc);
                printf(" RH:%.1f ", RH);
                printf(" TH:%.2f ", TH);
            }
            printf("\n");

            //if (gpx->alt > -400.0)
            {
                printf("    %9.2f ; %6.1f ; %6.1f ", gpx->alt, gpx->ptu_Rf1, gpx->ptu_Rf2);
                printf("; %10.6f ; %10.6f ; %10.6f ", gpx->ptu_calT1[0], gpx->ptu_calT1[1], gpx->ptu_calT1[2]);
                //printf(";  %8d ; %8d ; %8d ", meas[0], meas[1], meas[2]);
                printf("; %10.6f ; %10.6f ", gpx->ptu_calH[0], gpx->ptu_calH[1]);
                //printf(";  %8d ; %8d ; %8d ", meas[3], meas[4], meas[5]);
                printf("; %10.6f ; %10.6f ; %10.6f ", gpx->ptu_calT2[0], gpx->ptu_calT2[1], gpx->ptu_calT2[2]);
                //printf(";  %8d ; %8d ; %8d" , meas[6], meas[7], meas[8]);
                printf("\n");
            }
        }

    }

    return err;
}


static int get_GPSweek(gpx_t *gpx, int ofs) {
    int i;
    unsigned byte;
    ui8_t gpsweek_bytes[2];
    int gpsweek;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_GPSweek+ofs + i];
        gpsweek_bytes[i] = byte;
    }

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
    //if (gpsweek < 0) { gpx->week = -1; return -1; } // (short int)
    gpx->week = gpsweek;

    return 0;
}

//char weekday[7][3] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx, int ofs) {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime = 0, // 32bit
        day;
    int ms;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSiTOW+ofs + i];
        gpstime_bytes[i] = byte;
    }

    memcpy(&gpstime, gpstime_bytes, 4);

    gpx->tow_ms = gpstime;
    ms = gpstime % 1000;
    gpstime /= 1000;
    gpx->gpssec = gpstime;

    day = (gpstime / (24 * 3600)) % 7;
    //if ((day < 0) || (day > 6)) return -1;  // besser CRC-check

    gpstime %= (24*3600);

    gpx->wday = day;
    gpx->std =  gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek =  gpstime % 60 + ms/1000.0;
    gpx->isUTC = 0;

    return 0;
}

static int get_GPS1(gpx_t *gpx, int ofs) {
    int err=0;

    // gpx->frame[pos_GPS1+1] != (pck_GPS1 & 0xFF) ?
    err = check_CRC(gpx, pos_GPS1+ofs, pck_GPS1);
    if (err) {
        gpx->crc |= crc_GPS1;
        // reset GPS1-data (json)
        gpx->jahr = 0; gpx->monat = 0; gpx->tag = 0;
        gpx->std = 0; gpx->min = 0; gpx->sek = 0.0;
        gpx->isUTC = 0;
        return -1;
    }

    err |= get_GPSweek(gpx, ofs); // no plausibility-check
    err |= get_GPStime(gpx, ofs); // no plausibility-check

    return err;
}

static int get_GPS2(gpx_t *gpx, int ofs) {
    int err=0;

    // gpx->frame[pos_GPS2+1] != (pck_GPS2 & 0xFF) ?
    err = check_CRC(gpx, pos_GPS2+ofs, pck_GPS2);
    if (err) gpx->crc |= crc_GPS2;

    return err;
}

// WGS84/GRS80 Ellipsoid
#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

const
double a = EARTH_a,
       b = EARTH_b,
       a_b = EARTH_a2_b2,
       e2  = EARTH_a2_b2 / (EARTH_a*EARTH_a),
       ee2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);

static void ecef2elli(double X[], double *lat, double *lon, double *alt) {
    double phi, lam, R, p, t;

    lam = atan2( X[1] , X[0] );

    p = sqrt( X[0]*X[0] + X[1]*X[1] );
    t = atan2( X[2]*a , p*b );

    phi = atan2( X[2] + ee2 * b * sin(t)*sin(t)*sin(t) ,
                 p - e2 * a * cos(t)*cos(t)*cos(t) );

    R = a / sqrt( 1 - e2*sin(phi)*sin(phi) );
    *alt = p / cos(phi) - R;

    *lat = phi*180/M_PI;
    *lon = lam*180/M_PI;
}

static int get_ECEFkoord(gpx_t *gpx, int pos_ecef) {
    int i, k;
    unsigned byte;
    ui8_t XYZ_bytes[4];
    int XYZ; // 32bit
    double X[3], lat, lon, alt;
    ui8_t gpsVel_bytes[2];
    short vel16; // 16bit
    double V[3];
    double phi, lam, dir;
    double vN; double vE; double vU;


    for (k = 0; k < 3; k++) {

        for (i = 0; i < 4; i++) {
            byte = gpx->frame[pos_ecef + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        X[k] = XYZ / 100.0;

        for (i = 0; i < 2; i++) {
            byte = gpx->frame[pos_ecef+12 + 2*k + i];
            gpsVel_bytes[i] = byte;
        }
        vel16 = gpsVel_bytes[0] | gpsVel_bytes[1] << 8;
        V[k] = vel16 / 100.0;

    }


    // ECEF-Position
    ecef2elli(X, &lat, &lon, &alt);
    gpx->lat = lat;
    gpx->lon = lon;
    gpx->alt = alt;
    if ((alt < -1000) || (alt > 80000)) return -3; // plausibility-check: altitude, if ecef=(0,0,0)


    // ECEF-Velocities
    // ECEF-Vel -> NorthEastUp
    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    vE = -V[0]*sin(lam) + V[1]*cos(lam);
    vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    // NEU -> HorDirVer
    gpx->vH = sqrt(vN*vN+vE*vE);
/*
    double alpha;
    alpha = atan2(gpx->vN, gpx->vE)*180/M_PI;  // ComplexPlane (von x-Achse nach links) - GeoMeteo (von y-Achse nach rechts)
    dir = 90-alpha;                            // z=x+iy= -> i*conj(z)=y+ix=re(i(pi/2-t)), Achsen und Drehsinn vertauscht
    if (dir < 0) dir += 360;                   // atan2(y,x)=atan(y/x)=pi/2-atan(x/y) , atan(1/t) = pi/2 - atan(t)
    gpx->vD2 = dir;
*/
    dir = atan2(vE, vN) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    gpx->vV = vU;

    return 0;
}

static int get_GPS3(gpx_t *gpx, int ofs) {
    int err=0;

    // gpx->frame[pos_GPS3+1] != (pck_GPS3 & 0xFF) ?
    err = check_CRC(gpx, pos_GPS3+ofs, pck_GPS3);
    if (err) {
        gpx->crc |= crc_GPS3;
        // reset GPS3-data (json)
        gpx->lat = 0.0; gpx->lon = 0.0; gpx->alt = 0.0;
        gpx->vH  = 0.0; gpx->vD  = 0.0; gpx->vV  = 0.0;
        gpx->numSV = 0;
        return -1;
    }
    // pos_GPS3+2 = pos_GPSecefX
    err |= get_ECEFkoord(gpx, pos_GPS3+ofs+2); // plausibility-check: altitude, if ecef=(0,0,0)

    gpx->numSV = gpx->frame[pos_numSats+ofs];

    return err;
}

// GNSS1=8226
static int get_posdatetime(gpx_t *gpx, int pos_posdatetime) {
    int err=0;

    err = check_CRC(gpx, pos_posdatetime, pck_8226_POSDATETIME);
    if (err) {
        ///TODO: fw 0x50dd , ec < 0
        gpx->crc |= crc_GPS1 | crc_GPS3;
        // reset GPS1-data (json)
        gpx->jahr = 0; gpx->monat = 0; gpx->tag = 0;
        gpx->std = 0; gpx->min = 0; gpx->sek = 0.0;
        gpx->isUTC = 0;
        // reset GPS3-data (json)
        gpx->lat = 0.0; gpx->lon = 0.0; gpx->alt = 0.0;
        gpx->vH  = 0.0; gpx->vD  = 0.0; gpx->vV  = 0.0;
        gpx->numSV = 0;
        return -1;
    }

    // ublox M10 UBX-NAV-POSECEF (0x01 0x01) ?
    err |= get_ECEFkoord(gpx, pos_posdatetime+2); // plausibility-check: altitude, if ecef=(0,0,0)

    // ublox M10 UBX-NAV-PVT (0x01 0x07) ? (UTC)
    // date
    gpx->jahr  = gpx->frame[pos_posdatetime+20] | gpx->frame[pos_posdatetime+21]<<8;
    gpx->monat = gpx->frame[pos_posdatetime+22];
    gpx->tag   = gpx->frame[pos_posdatetime+23];
    // time
    gpx->std = gpx->frame[pos_posdatetime+24];
    gpx->min = gpx->frame[pos_posdatetime+25];
    gpx->sek = gpx->frame[pos_posdatetime+26];
    if (gpx->frame[pos_posdatetime+27] < 100) gpx->sek += gpx->frame[pos_posdatetime+27]/100.0;
    //
    gpx->isUTC = 1;

    ///TODO: numSV/fixOK
    //gpx->numSV = gpx->frame[pos_numSats+ofs];

    return err;
}

// GNSS2=8329
static int get_gnssSVs(gpx_t *gpx, int pos_gnss2) {
    int err=0;

    memset(gpx->gnss_sv, 0, 32*sizeof(gnss_t));
    gpx->gnss_numSVb168 = 0;
    gpx->gnss_nSVstatus = 0;

    err = check_CRC(gpx, pos_gnss2, pck_8329_SATS);

    if (!err) {
        // ublox M10 UBX-NAV-SAT (0x01 0x35) ?
        // ublox M10 UBX-NAV-SIG (0x01 0x43) ?

        // int pos_gnss1 = 161;
        // int pos_gnss2 = 203; == pos_posgnss
        // int pos_zero  = 248;

        int cntSV168 = 0; // 21*8 bits
        // 00..31: GPS, PRN+1
        // 32..67: GALILEO, GAL_E + 31
        for (int j = 0; j < 21; j++) {
            int b = gpx->frame[pos_gnss2+2+4+j];
            for (int n = 0; n < 8; n++) {  //DBG fprintf(stdout, "%d", (b>>n)&1);
                int s = (b>>n)&1;
                if (s) {
                    ui8_t svid = j*8+n + 1;
                    if (cntSV168 < 32) {
                        gpx->gnss_sv[cntSV168].id168 = svid;
                        //DBG fprintf(stdout, " %3d", svid);
                    }
                    cntSV168 += 1;
                }
            }
        }
        gpx->gnss_numSVb168 = cntSV168;

        int cntSVstatus = 0; // max 16*2
        for (int j = 0; j < 16; j++) {
            ui8_t b = gpx->frame[pos_gnss2+2+4+21+j];
            ui8_t b0 = b & 0xF;       // b & 0x0F
            ui8_t b1 = (b>>4) & 0xF;  // b & 0xF0
            gpx->gnss_sv[2*j  ].status = b0; if (b0) cntSVstatus++;
            gpx->gnss_sv[2*j+1].status = b1; if (b1) cntSVstatus++;
        }
        gpx->gnss_nSVstatus = cntSVstatus;

        //check: cntSV168 == cntSVstatus ?

        ///TODO: numSV/fixOK
        //       used in solution / tracked / searched / visible ?
        gpx->numSV = gpx->gnss_nSVstatus; // == gpx->gnss_numSVb168 ?
    }
    else {
        ///TODO: fw 0x50dd , ec < 0
        gpx->crc |= crc_GPS2;
    }

    return err;
}

static int prn_gnss_sat2(gpx_t *gpx) {
    int n;

    fprintf(stdout, "\n");
    fprintf(stdout, "  numSV168 : %2d", gpx->gnss_numSVb168);
    fprintf(stdout, "  nSVstatus: %2d", gpx->gnss_nSVstatus);
    // DBG fprintf(stdout, "  # %d #", gpx->nss_numSV168 - gpx->gnss_nSVstatus);
    fprintf(stdout, "\n");
    fprintf(stdout, "  SVids: ");
    for (n = 0; n < 32; n++) {
        if (n < gpx->gnss_numSVb168) fprintf(stdout, " %3d", gpx->gnss_sv[n].id168);
        if (n < gpx->gnss_nSVstatus) fprintf(stdout, ":%X", gpx->gnss_sv[n].status);
    }
    fprintf(stdout, "\n");

    for (n = 0; n < 32; n++) {
        if (n < gpx->gnss_numSVb168 || n < gpx->gnss_nSVstatus) {
            if (gpx->gnss_sv[n].id168 < 33) { // 01..32 (GPS ?)
                ui8_t prnGPS = gpx->gnss_sv[n].id168;
                if (n == 0 && gpx->gnss_sv[n].id168 < 33) fprintf(stdout, "  GPS: ");
                //fprintf(stdout, "  GPS PRN%02d: %X\n", prnGPS, gpx->gnss_sv[n].status);
                //fprintf(stdout, "  GPS PRN%02d", prnGPS);
                fprintf(stdout, " PRN%02d", prnGPS);
            }
            else if (gpx->gnss_sv[n].id168 < 33+36) { // 33..68 -> 01..36 (GALILEO ??)
                ui8_t prnGAL = gpx->gnss_sv[n].id168 - 32;
                if (n == 0 || n > 0 && gpx->gnss_sv[n-1].id168 < 33) {
                    if (n > 0) fprintf(stdout, "\n");
                    fprintf(stdout, "  GAL: ");
                }
                //fprintf(stdout, "  GAL E%02d: %X\n", prnGAL, gpx->gnss_sv[n].status);
                //fprintf(stdout, "  GAL E%02d", prnGAL);
                fprintf(stdout, " E%02d", prnGAL);
            }
        }
    }
    fprintf(stdout, "\n");

    return 0;
}

static int get_Aux(gpx_t *gpx, int out, int pos) {
//
// "Ozone Sounding with Vaisala Radiosonde RS41" user's guide M211486EN
//
    int auxlen, auxcrc, count7E, pos7E;
    int i, n;

    n = 0;
    count7E = 0;
    pos7E = 0;
    //if (pos != pos_AUX) ;
    gpx->xdata[0] = '\0';

    if (frametype(gpx) <= 0) // pos7E == pos7611, 0x7E^0x76=0x08 ...
    {
        // 7Exx: xdata
        while ( pos < FRAME_LEN  &&  gpx->frame[pos] == 0x7E ) {

            auxlen = gpx->frame[pos+1];
            auxcrc = gpx->frame[pos+2+auxlen] | (gpx->frame[pos+2+auxlen+1]<<8);

            if ( pos + auxlen + 4 <= FRAME_LEN && auxcrc == crc16(gpx->frame+pos+2, auxlen) ) {
                if (count7E == 0) {
                    if (out) fprintf(stdout, "\n # xdata = ");
                }
                else {
                    if (out) fprintf(stdout, " # ");
                    gpx->xdata[n++] = '#'; // aux separator
                }

                //fprintf(stdout, " # %02x : ", gpx->frame[pos7E+2]);
                for (i = 1; i < auxlen; i++) {
                    ui8_t c = gpx->frame[pos+2+i]; // (char) or better < 0x7F
                    if (c > 0x1E && c < 0x7F) {      // ASCII-only
                        if (out) fprintf(stdout, "%c", c);
                        gpx->xdata[n++] = c;
                    }
                }
                count7E++;
                pos7E = pos;
                pos += 2+auxlen+2;
            }
            else {
                pos = FRAME_LEN;
                gpx->crc |= crc_AUX;
            }
        }
    }
    gpx->xdata[n] = '\0';

    i = check_CRC(gpx, pos, pck_ZERO);  // 0x76xx: 00-padding block
    if (i) gpx->crc |= crc_ZERO;

    return pos7E;  // count7E
}

static int get_Calconf(gpx_t *gpx, int out, int ofs) {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    ui16_t fw = 0;
    int freq = 0, f0 = 0, f1 = 0;
    char sondetyp[9];
    char rsmtyp[10];
    int err = 0;

    byte = gpx->frame[pos_CalData+ofs];
    calfr = byte;
    err = check_CRC(gpx, pos_FRAME+ofs, pck_FRAME);

    if (out && gpx->option.vbs == 3) {
        fprintf(stdout, "\n");  // fflush(stdout);
        fprintf(stdout, "[%5d] ", gpx->frnr);
        fprintf(stdout, " 0x%02x: ", calfr);
        for (i = 0; i < 16; i++) {
            byte = gpx->frame[pos_CalData+ofs+1+i];
            fprintf(stdout, "%02x ", byte);
        }
        /*
        if (err == 0) fprintf(stdout, "[OK]");
        else          fprintf(stdout, "[NO]");
        */
        fprintf(stdout, " ");
    }

    if (err == 0)
    {
        if (calfr == 0x00) {
            byte = gpx->frame[pos_Calfreq+ofs] & 0xC0;  // erstmal nur oberste beiden bits
            f0 = (byte * 10) / 64;  // 0x80 -> 1/2, 0x40 -> 1/4 ; dann mal 40
            byte = gpx->frame[pos_Calfreq+ofs+1];
            f1 = 40 * byte;
            freq = 400000 + f1+f0; // kHz;
            if (out && gpx->option.vbs) fprintf(stdout, ": fq %d ", freq);
            gpx->freq = freq;
        }

        if (calfr == 0x01) {
            fw = gpx->frame[pos_CalData+ofs+6] | (gpx->frame[pos_CalData+ofs+7]<<8);
            if (out && gpx->option.vbs) fprintf(stdout, ": fw 0x%04x ", fw);
            gpx->conf_fw = fw;
        }

        if (calfr == 0x02) {    // 0x5E, 0x5A..0x5B
            ui8_t  bk = gpx->frame[pos_Calburst+ofs];  // fw >= 0x4ef5, burst-killtimer in 0x31 relevant
            ui16_t kt = gpx->frame[pos_CalData+ofs+8] + (gpx->frame[pos_CalData+ofs+9] << 8); // killtimer (short?)
            if (out && gpx->option.vbs) fprintf(stdout, ": BK %02X ", bk);
            if (out && gpx->option.vbs && kt != 0xFFFF ) fprintf(stdout, ": kt %.1fmin ", kt/60.0);
            gpx->conf_bk = bk;
            gpx->conf_kt = kt;
        }

        if (calfr == 0x31) {    // 0x59..0x5A
            ui16_t bt = gpx->frame[pos_CalData+ofs+7] + (gpx->frame[pos_CalData+ofs+8] << 8); // burst timer (short?)
            // fw >= 0x4ef5: default=[88 77]=0x7788sec=510min
            if (out  && bt != 0x0000 &&
                    (gpx->option.vbs == 3  ||  gpx->option.vbs && gpx->conf_bk)
               ) fprintf(stdout, ": bt %.1fmin ", bt/60.0);
            gpx->conf_bt = bt;
        }

        if (calfr == 0x32) {
            ui16_t cd = gpx->frame[pos_CalData+ofs+1] + (gpx->frame[pos_CalData+ofs+2] << 8); // countdown (bt or kt) (short?)
            if (out && cd != 0xFFFF &&
                    (gpx->option.vbs == 3  ||  gpx->option.vbs && (gpx->conf_bk || gpx->conf_kt != 0xFFFF))
               ) fprintf(stdout, ": cd %.1fmin ", cd/60.0);
            gpx->conf_cd = cd;  // (short/i16_t) ?
        }

        if (calfr == 0x21) {  // ... eventuell noch 2 bytes in 0x22
            for (i = 0; i < 9; i++) sondetyp[i] = 0;
            for (i = 0; i < 8; i++) {
                byte = gpx->frame[pos_CalRSTyp+ofs + i];
                if ((byte >= 0x20) && (byte < 0x7F)) sondetyp[i] = byte;
                else if (byte == 0x00) sondetyp[i] = '\0';
            }
            if (out && gpx->option.vbs) fprintf(stdout, ": %s ", sondetyp);
            strcpy(gpx->rstyp, sondetyp);
            if (out && gpx->option.vbs == 3) { // Stationsdruck QFE
                float qfe1 = 0.0, qfe2 = 0.0;
                memcpy(&qfe1, gpx->frame+pos_CalData+1, 4);
                memcpy(&qfe2, gpx->frame+pos_CalData+5, 4);
                if (qfe1 > 0.0 || qfe2 > 0.0) {
                    fprintf(stdout, " ");
                    if (qfe1 > 0.0) fprintf(stdout, "QFE1:%.1fhPa ", qfe1);
                    if (qfe2 > 0.0) fprintf(stdout, "QFE2:%.1fhPa ", qfe2);
                }
            }
        }

        if (calfr == 0x22) {
            for (i = 0; i < 10; i++) rsmtyp[i] = 0;
            for (i = 0; i < 8; i++) {
                byte = gpx->frame[pos_CalRSM+ofs + i];
                if ((byte >= 0x20) && (byte < 0x7F)) rsmtyp[i] = byte;
                else /*if (byte == 0x00)*/ rsmtyp[i] = '\0';
            }
            if (out && gpx->option.vbs) fprintf(stdout, ": %s ", rsmtyp);
            strcpy(gpx->rsm, rsmtyp);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------------------ */

#define rs_N 255
#define rs_R 24
#define rs_K (rs_N-rs_R)

static int rs41_ecc(gpx_t *gpx, int frmlen) {
// richtige framelen wichtig fuer 0-padding

    int i, leak, ret = 0;
    int errors1, errors2;
    ui8_t cw1[rs_N], cw2[rs_N];
    ui8_t err_pos1[rs_R], err_pos2[rs_R],
          err_val1[rs_R], err_val2[rs_R];

    memset(cw1, 0, rs_N);
    memset(cw2, 0, rs_N);

    if (frmlen > FRAME_LEN) frmlen = FRAME_LEN;
    //cfg_rs41.frmlen = frmlen;
    //cfg_rs41.msglen = (frmlen-56)/2; // msgpos=56;
    leak = frmlen % 2;

    for (i = frmlen; i < FRAME_LEN; i++) gpx->frame[i] = 0;  // FRAME_LEN-HDR = 510 = 2*255


    for (i = 0; i < rs_R; i++) cw1[i] = gpx->frame[cfg_rs41.parpos+i     ];
    for (i = 0; i < rs_R; i++) cw2[i] = gpx->frame[cfg_rs41.parpos+i+rs_R];
    for (i = 0; i < rs_K; i++) cw1[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i  ];
    for (i = 0; i < rs_K; i++) cw2[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i+1];

    errors1 = rs_decode(&gpx->RS, cw1, err_pos1, err_val1);
    errors2 = rs_decode(&gpx->RS, cw2, err_pos2, err_val2);


    if (gpx->option.ecc == 2 && (errors1 < 0 || errors2 < 0))
    {   // 2nd pass: set packet-IDs
        gpx->frame[pos_FRAME] = (pck_FRAME>>8)&0xFF; gpx->frame[pos_FRAME+1] = pck_FRAME&0xFF;
        gpx->frame[pos_PTU]   = (pck_PTU  >>8)&0xFF; gpx->frame[pos_PTU  +1] = pck_PTU  &0xFF;
        gpx->frame[pos_GPS1]  = (pck_GPS1 >>8)&0xFF; gpx->frame[pos_GPS1 +1] = pck_GPS1 &0xFF;
        gpx->frame[pos_GPS2]  = (pck_GPS2 >>8)&0xFF; gpx->frame[pos_GPS2 +1] = pck_GPS2 &0xFF;
        gpx->frame[pos_GPS3]  = (pck_GPS3 >>8)&0xFF; gpx->frame[pos_GPS3 +1] = pck_GPS3 &0xFF;
        // AUX-frames mit vielen Fehlern besser mit 00 auffuellen
        // std-O3-AUX-frame: NDATA+7
        if (frametype(gpx) < -2) {  // ft >= 0: NDATA_LEN , ft < 0: FRAME_LEN
            for (i = NDATA_LEN + 7; i < FRAME_LEN-2; i++) gpx->frame[i] = 0;
        }
        else { // std-frm (len=320): std_ZERO-frame (7611 00..00 ECC7)
            for (i = NDATA_LEN; i < FRAME_LEN; i++) gpx->frame[i] = 0;
            gpx->frame[pos_ZEROstd  ] = 0x76;  // pck_ZEROstd
            gpx->frame[pos_ZEROstd+1] = 0x11;  // pck_ZEROstd
            for (i = pos_ZEROstd+2; i < NDATA_LEN-2; i++) gpx->frame[i] = 0;
            gpx->frame[NDATA_LEN-2] = 0xEC;    // crc(pck_ZEROstd)
            gpx->frame[NDATA_LEN-1] = 0xC7;    // crc(pck_ZEROstd)
        }
        for (i = 0; i < rs_K; i++) cw1[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i  ];
        for (i = 0; i < rs_K; i++) cw2[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i+1];
        errors1 = rs_decode(&gpx->RS, cw1, err_pos1, err_val1);
        errors2 = rs_decode(&gpx->RS, cw2, err_pos2, err_val2);
    }


    // Wenn Fehler im 00-padding korrigiert wurden,
    // war entweder der frame zu kurz, oder
    // Fehler wurden falsch korrigiert;
    // allerdings ist bei t=12 die Wahrscheinlichkeit,
    // dass falsch korrigiert wurde mit 1/t! sehr gering.

    // check CRC32
    // CRC32 OK:
    //for (i = 0; i < cfg_rs41.hdrlen; i++) frame[i] = data[i];
    for (i = 0; i < rs_R; i++) {
        gpx->frame[cfg_rs41.parpos+     i] = cw1[i];
        gpx->frame[cfg_rs41.parpos+rs_R+i] = cw2[i];
    }
    for (i = 0; i < rs_K; i++) { // cfg_rs41.msglen <= rs_K
        gpx->frame[cfg_rs41.msgpos+  2*i] = cw1[rs_R+i];
        gpx->frame[cfg_rs41.msgpos+1+2*i] = cw2[rs_R+i];
    }
    if (leak) {
        gpx->frame[cfg_rs41.msgpos+2*i] = cw1[rs_R+i];
    }


    ret = errors1 + errors2;
    if (errors1 < 0 || errors2 < 0) {
        ret = 0;
        if (errors1 < 0) ret |= 0x1;
        if (errors2 < 0) ret |= 0x2;
        ret = -ret;
    }

    return ret;
}

/* ------------------------------------------------------------------------------------ */

static int prn_frm(gpx_t *gpx) {
    fprintf(stdout, "[%5d] ", gpx->frnr);
    fprintf(stdout, "(%s) ", gpx->id);
    if (gpx->option.vbs == 3) fprintf(stdout, "(%.1f V) ", gpx->batt);
    fprintf(stdout, " ");
    return 0;
}

static int prn_ptu(gpx_t *gpx) {
    fprintf(stdout, " ");
    if (gpx->T > -273.0) fprintf(stdout, " T=%.1fC ", gpx->T);
    if (gpx->RH > -0.5 && gpx->option.ptu != 2)  fprintf(stdout, " _RH=%.0f%% ", gpx->RH);
    if (gpx->P > 0.0) {
        if (gpx->P < 100.0) fprintf(stdout, " P=%.2fhPa ", gpx->P);
        else                fprintf(stdout, " P=%.1fhPa ", gpx->P);
    }
    if (gpx->option.ptu == 2) {
        if (gpx->RH2 > -0.5)  fprintf(stdout, " RH2=%.0f%% ", gpx->RH2);
    }

    // dew point
    if (gpx->option.dwp)
    {
        float rh = gpx->RH;
        float Td = -273.15f; // dew point Td
        if (gpx->option.ptu == 2) rh = gpx->RH2;
        if (rh > 0.0f && gpx->T > -273.0f) {
            float gamma = logf(rh / 100.0f) + (17.625f * gpx->T / (243.04f + gpx->T));
            Td = 243.04f * gamma / (17.625f - gamma);
            fprintf(stdout, " Td=%.1fC ", Td);
        }
    }
    return 0;
}

static int prn_gpstime(gpx_t *gpx) {
    //Gps2Date(gpx);
    fprintf(stdout, "%s ", weekday[gpx->wday]);
    fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%06.3f",
            gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
    if (gpx->option.vbs == 3) fprintf(stdout, " (W %d)", gpx->week);
    fprintf(stdout, " ");
    return 0;
}

static int prn_gpspos(gpx_t *gpx) {
    //fprintf(stdout, " ");
    fprintf(stdout, " lat: %.5f ", gpx->lat);
    fprintf(stdout, " lon: %.5f ", gpx->lon);
    fprintf(stdout, " alt: %.2f ", gpx->alt);
    fprintf(stdout, "  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vV);
    if (gpx->option.vbs == 3) fprintf(stdout, " sats: %02d ", gpx->numSV);
    return 0;
}

static int prn_posdatetime(gpx_t *gpx) {
    //Gps2Date(gpx);
    //fprintf(stdout, "%s ", weekday[gpx->wday]);
    fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%05.2f",
            gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
    //if (gpx->option.vbs == 3) fprintf(stdout, " (W %d)", gpx->week);
    fprintf(stdout, " ");

    fprintf(stdout, " ");
    fprintf(stdout, " lat: %.5f ", gpx->lat);
    fprintf(stdout, " lon: %.5f ", gpx->lon);
    fprintf(stdout, " alt: %.2f ", gpx->alt);
    fprintf(stdout, "  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vV);

    if (gpx->option.vbs == 3) fprintf(stdout, " sats: %02d ", gpx->numSV); ///TODO: used/tracked/searched/visible ?

    return 0;
}

static int prn_sat1(gpx_t *gpx, int ofs) {

    fprintf(stdout, "\n");

    fprintf(stdout, "iTOW: 0x%08X", u4(gpx->frame+pos_GPSiTOW+ofs));
    fprintf(stdout, "  week: 0x%04X", u2(gpx->frame+pos_GPSweek+ofs));

    return 0;
}
const double c = 299.792458e6;
const double L1 = 1575.42e6;
static int prn_sat2(gpx_t *gpx, int ofs) {
    int i, n;
    int sv;
    ui32_t minPR;

    fprintf(stdout, "\n");

    minPR = u4(gpx->frame+pos_minPR+ofs);
    fprintf(stdout, "minPR: %d", minPR);
    fprintf(stdout, "\n");

    for (i = 0; i < 12; i++) {
        n = i*7;
        sv = gpx->frame[pos_satsN+ofs+2*i];
        if (sv == 0xFF) break;
        fprintf(stdout, "    SV: %2d ", sv);
        //fprintf(stdout, " (%02x) ", gpx->frame[pos_satsN+2*i+1]);
        fprintf(stdout, "#  ");
        fprintf(stdout, "prMes: %.1f", u4(gpx->frame+pos_dataSats+ofs+n)/100.0 + minPR);
        fprintf(stdout, "  ");
        fprintf(stdout, "doMes: %.1f", -i3(gpx->frame+pos_dataSats+ofs+n+4)/100.0*L1/c);
        fprintf(stdout, "\n");
    }

    return 0;
}
static int prn_sat3(gpx_t *gpx, int ofs) {
    int numSV;
    double pDOP, sAcc;

    fprintf(stdout, "\n");

    fprintf(stdout, "ECEF-POS: (%d,%d,%d)\n",
                     (i32_t)u4(gpx->frame+pos_GPSecefX+ofs),
                     (i32_t)u4(gpx->frame+pos_GPSecefY+ofs),
                     (i32_t)u4(gpx->frame+pos_GPSecefZ+ofs));
    fprintf(stdout, "ECEF-VEL: (%d,%d,%d)\n",
                     (i16_t)u2(gpx->frame+pos_GPSecefV+ofs+0),
                     (i16_t)u2(gpx->frame+pos_GPSecefV+ofs+2),
                     (i16_t)u2(gpx->frame+pos_GPSecefV+ofs+4));

    numSV = gpx->frame[pos_numSats+ofs];
    sAcc = gpx->frame[pos_sAcc+ofs]/10.0; if (gpx->frame[pos_sAcc+ofs] == 0xFF) sAcc = -1.0;
    pDOP = gpx->frame[pos_pDOP+ofs]/10.0; if (gpx->frame[pos_pDOP+ofs] == 0xFF) pDOP = -1.0;
    fprintf(stdout, "numSatsFix: %2d  sAcc: %.1f  pDOP: %.1f\n", numSV, sAcc, pDOP);

    /*
    fprintf(stdout, "CRC: ");
    fprintf(stdout, " %04X", pck_GPS1);
    if (check_CRC(gpx, pos_GPS1+ofs, pck_GPS1)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(gpx, pos_GPS1, pck_GPS1));
    fprintf(stdout, " %04X", pck_GPS2);
    if (check_CRC(gpx, pos_GPS2+ofs, pck_GPS2)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(gpx, pos_GPS2, pck_GPS2));
    fprintf(stdout, " %04X", pck_GPS3);
    if (check_CRC(gpx, pos_GPS3+ofs, pck_GPS3)==0) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    //fprintf(stdout, "[%+d]", check_CRC(gpx, pos_GPS3, pck_GPS3));

    fprintf(stdout, "\n");
    */
    return 0;
}

static int print_position(gpx_t *gpx, int ec) {
    int i;
    int err = 1;
    int err0 = 1, err1 = 1, err2 = 1, err3 = 1, err13 = 1;
    //int output, out_mask;
    int encrypted = 0;
    int unexp = 0;
    int out = 1;
    int sat = 0;
    int pos_aux = 0, cnt_aux = 0;
    int ofs_ptu = 0, pck_ptu = 0;
    int isGNSS2 = 0;
    int ret = 0;

    //gpx->out = 0;
    gpx->aux = 0;

    if (gpx->option.sat) sat = 1;
    if (gpx->option.slt) out = 0; else out = 1;

    if ( ec >= 0 )
    {
        int pos, blk, len, crc, pck;
        int flen = NDATA_LEN;

        int ofs_cal = 0;
        int frm_end = NDATA_LEN-2;

        if (frametype(gpx) < 0) flen += XDATA_LEN;

        switch (gpx->frame[pos_PTU]) {
            case 0x7A: // 0x7A2A
                    frm_end = flen-2;
                    break;
            case 0x7F: // 0x7F1B
                    frm_end = pos_ZEROstd + 0x1B-0x2A - 2;
                    break;
            case 0x80: // 0x80A7
                    frm_end = pos_PTU + 2 + 0xA7;
                    break;
        }

        pos = pos_FRAME;
        gpx->crc = 0;

        while (pos < flen-1) {
            blk = gpx->frame[pos];
            len = gpx->frame[pos+1];
            crc = check_CRC(gpx, pos, blk<<8);
            pck = (blk<<8) | len;

            if ( crc == 0 )  // ecc-OK -> crc-OK
            {
                int ofs = 0;
                switch (pck)
                {
                    case pck_FRAME: // 0x7928
                            ofs = pos - pos_FRAME;
                            ofs_cal = ofs;
                            err = get_FrameConf(gpx, ofs);
                            if ( !err ) {
                                if (out || sat) prn_frm(gpx);
                            }
                            break;

                    case pck_PTU: // 0x7A2A
                            ofs_ptu = pos - pos_PTU;
                            pck_ptu = pck_PTU;
                            if ( 0 && gpx->option.ptu ) {
                                //err0 = get_PTU(gpx, ofs_ptu, pck_ptu);
                                // if (!err0) prn_ptu(gpx);
                            }
                            break;

                    case pck_GPS1: // 0x7C1E
                            ofs = pos - pos_GPS1;
                            err1 = get_GPS1(gpx, ofs);
                            if ( !err1 ) {
                                Gps2Date(gpx);
                                if (out) prn_gpstime(gpx);
                                if (sat) prn_sat1(gpx, ofs);
                            }
                            break;

                    case pck_GPS2: // 0x7D59
                            ofs = pos - pos_GPS2;
                            err2 = get_GPS2(gpx, ofs);
                            if ( !err2 ) {
                                if (sat) prn_sat2(gpx, ofs);
                            }
                            break;

                    case pck_GPS3: // 0x7B15
                            ofs = pos - pos_GPS3;
                            err3 = get_GPS3(gpx, ofs);
                            if ( !err3 ) {
                                if (out) prn_gpspos(gpx);
                                if (sat) prn_sat3(gpx, ofs);
                            }
                            break;

                    case pck_SGM_xTU: // 0x7F1B
                            ofs_ptu = pos - pos_PTU;
                            pck_ptu = pck;
                            if ( 0 ) {
                                //err0 = get_PTU(gpx, ofs_ptu, pck_ptu);
                            }
                            break;

                    case pck_SGM_CRYPT: // 0x80A7
                            encrypted = 1;
                            if (out) fprintf(stdout, " [%04X] (RS41-SGM) ", pck_SGM_CRYPT);
                            break;

                    case pck_960A: // 0x960A
                            // ? 64 bit data integrity and authenticity ?
                            break;

                    case pck_8226_POSDATETIME: // 0x8226
                            err13 = get_posdatetime(gpx, pos);
                            if ( !err13 ) {
                                if (out) prn_posdatetime(gpx);
                            }
                            break;

                    case pck_8329_SATS: // 0x8329
                            err2 = get_gnssSVs(gpx, pos);
                            isGNSS2 = 1;
                            ////if ( !err2 ) { if (sat) prn_gnss_sat2(gpx); }
                            break;

                    default:
                            if (blk == 0x7E) {
                                if (pos_aux == 0) pos_aux = pos; // pos == pos_AUX ?
                                cnt_aux += 1;
                            }
                            if (blk == 0x76) {
                                // ZERO-Padding pck
                            }

                            if (blk != 0x76 && blk != 0x7E) {
                                if (out) fprintf(stdout, " [%04X] ", pck);
                                unexp = 1;
                            }
                }
            }
            else { // CRC-ERROR (ECC-OK)
                fprintf(stdout, " [ERROR]\n");
                break;
            }

            pos += 2+len+2; // next pck

            if ( pos > frm_end )  // end of (sub)frame
            {
                if (gpx->option.ptu && !sat && !encrypted && pck_ptu > 0) {
                    err0 = get_PTU(gpx, ofs_ptu, pck_ptu, !err3);
                    if (!err0 && out) prn_ptu(gpx);
                }
                pck_ptu = 0;

                get_Calconf(gpx, out, ofs_cal);

                if (out && ec > 0 && pos > flen-1) fprintf(stdout, " (%d)", ec);

                if (pos_aux) gpx->aux = get_Aux(gpx, out && gpx->option.vbs > 1, pos_aux);

                gpx->crc = 0;
                frm_end = FRAME_LEN-2;

                if ( isGNSS2 ) {
                    if (sat && !err2) prn_gnss_sat2(gpx);
                }

                if (out || sat) fprintf(stdout, "\n");


                if (gpx->option.jsn) {
                    // Print out telemetry data as JSON
                    if ( !err && ((!err1 && !err3) || !err13 || encrypted) ) { // frame-nb/id && gps-time && gps-position  (crc-)ok; 3 CRCs, RS not needed
                        // eigentlich GPS, d.h. UTC = GPS - 18sec (ab 1.1.2017)
                        fprintf(stdout, "{ \"type\": \"%s\"", "RS41");
                        fprintf(stdout, ", \"frame\": %d, \"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f, \"sats\": %d, \"bt\": %d, \"batt\": %.2f",
                                       gpx->frnr, gpx->id, gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV, gpx->numSV, gpx->conf_cd, gpx->batt );
                        if (gpx->option.ptu && !err0) {
                            float _RH = gpx->RH;
                            if (gpx->option.ptu == 2) _RH = gpx->RH2;
                            if (gpx->T > -273.0) {
                                fprintf(stdout, ", \"temp\": %.1f",  gpx->T );
                            }
                            if (_RH > -0.5) {
                                fprintf(stdout, ", \"humidity\": %.1f",  _RH );
                            }
                            if (gpx->P > 0.0) {
                                fprintf(stdout, ", \"pressure\": %.2f",  gpx->P );
                            }
                        }
                        if (gpx->aux) { // <=> gpx->xdata[0]!='\0'
                            fprintf(stdout, ", \"aux\": \"%s\"",  gpx->xdata );
                        }
                        if (encrypted) {
                            fprintf(stdout, ", \"subtype\": \"RS41-SGM\", \"encrypted\": true");
                        } else {
                            fprintf(stdout, ", \"subtype\": \"%s\"",  *gpx->rstyp ? gpx->rstyp : "RS41" );  // RS41-SG(P/M)
                            if (strncmp(gpx->rstyp, "RS41-SGM", 8) == 0) {
                                fprintf(stdout, ", \"encrypted\": false");
                            }
                        }
                        if (gpx->jsn_freq > 0) {  // rs41-frequency: gpx->freq
                            int fq_kHz = gpx->jsn_freq;
                            if (gpx->freq > 0) fq_kHz = gpx->freq;
                            fprintf(stdout, ", \"freq\": %d", fq_kHz);
                        }
                        if (*gpx->rsm) {  // RSM type
                            fprintf(stdout, ", \"rs41_mainboard\": \"%s\"", gpx->rsm);
                        }
                        if (gpx->conf_fw) {  // firmware
                            fprintf(stdout, ", \"rs41_mainboard_fw\": %d", gpx->conf_fw);
                        }


                        // Include frequency derived from subframe information if available.
                        if (gpx->freq > 0) {
                            fprintf(stdout, ", \"tx_frequency\": %d", gpx->freq );
                        }

                        // Reference time/position      (fw 0x50dd: datetime UTC)
                        fprintf(stdout, ", \"ref_datetime\": \"%s\"", gpx->isUTC ? "UTC" : "GPS" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                        fprintf(stdout, ", \"ref_position\": \"%s\"", "GPS" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

                        fprintf(stdout, " }\n");
                        fprintf(stdout, "\n");
                    }
                }
            }
        }
        ret = 1;
    }
    // else
    if (ec < 0 && (out || sat /*|| gpx->option.jsn*/)) {
        //
        // crc-OK pcks ?
        //
        int pck, ofs;
        int output = 0, out_mask;

        gpx->crc = 0;
        out_mask = crc_FRAME|crc_GPS1|crc_GPS3;
        if (gpx->option.ptu) out_mask |= crc_PTU;

        err = get_FrameConf(gpx, 0);
        if (out && !err) {
            prn_frm(gpx);
            output = 1;
        }

        pck = (gpx->frame[pos_PTU]<<8) | gpx->frame[pos_PTU+1];
        ofs = 0;
        ///TODO: fw 0x50dd

        if (pck < 0x8000) {
            //err0 = get_PTU(gpx, 0, pck, 0);
            if      (pck == pck_PTU)     ofs = 0;
            else if (pck == pck_SGM_xTU) ofs = 0x1B-0x2A;

            err1 = get_GPS1(gpx, ofs);
            err2 = get_GPS2(gpx, ofs);
            err3 = get_GPS3(gpx, ofs);
            if (!err1) Gps2Date(gpx);

            err0 = get_PTU(gpx, 0, pck, !err3);

            if (out) {

                if (!err1) prn_gpstime(gpx);
                if (!err3) prn_gpspos(gpx);
                if (!err0 && gpx->option.ptu) prn_ptu(gpx);
                if (0 && !err) get_Calconf(gpx, out, 0); // only if ecc-OK

                output = ((gpx->crc & out_mask) != out_mask);

                if (output) {
                    fprintf(stdout, " ");
                    fprintf(stdout, "[");
                    for (i=0; i<5; i++) fprintf(stdout, "%d", (gpx->crc>>i)&1);
                    fprintf(stdout, "]");
                }
            }
        }
        else if (pck == pck_SGM_CRYPT) {
            if (out && !err) {
                fprintf(stdout, " [%04X] (RS41-SGM) ", pck_SGM_CRYPT);
                //fprintf(stdout, "[%d] ", check_CRC(gpx, pos_PTU, pck_SGM_CRYPT));
                output = 1;
            }
        }

        if (out && output)
        {
            if      (ec == -1)  fprintf(stdout, " (-+)");
            else if (ec == -2)  fprintf(stdout, " (+-)");
            else   /*ec == -3*/ fprintf(stdout, " (--)");

            fprintf(stdout, "\n");  // fflush(stdout);
        }

        ret = output;
    }


    return ret;
}

static void print_frame(gpx_t *gpx, int len, dsp_t *dsp) {
    int i, ec = 0, ft;
    int ret = 0;

    gpx->crc = 0;

    // len < NDATA_LEN: EOF
    if (len < pos_GPS1) { // else: try prev.frame
        for (i = len; i < FRAME_LEN; i++) gpx->frame[i] = 0;
    }

    //frame[pos_FRAME-1] == 0x0F: len == NDATA_LEN(320)
    //frame[pos_FRAME-1] == 0xF0: len == FRAME_LEN(518)
    ft = frametype(gpx);
    if (ft >= 0) len = NDATA_LEN;  // ft >= 0: NDATA_LEN (default)
    else         len = FRAME_LEN;  // ft <  0: FRAME_LEN (aux)


    if (gpx->option.ecc) {
        ec = rs41_ecc(gpx, len);
    }


    if (gpx->option.raw) {
        for (i = 0; i < len; i++) {
            fprintf(stdout, "%02x", gpx->frame[i]);
        }
        if (gpx->option.ecc) {
            if (ec >= 0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            if (gpx->option.ecc /*== 2*/) {
                if (ec > 0) fprintf(stdout, " (%d)", ec);
                if (ec < 0) {
                    if      (ec == -1)  fprintf(stdout, " (-+)");
                    else if (ec == -2)  fprintf(stdout, " (+-)");
                    else   /*ec == -3*/ fprintf(stdout, " (--)");
                }
            }
        }
        fprintf(stdout, "\n");
    }
    else {
        pthread_mutex_lock( dsp->thd->mutex );
        //fprintf(stdout, "<%d> ", dsp->thd->tn);
        fprintf(stdout, "<%d: ", dsp->thd->tn);
        fprintf(stdout, "s=%+.4f, ", dsp->mv);
        fprintf(stdout, "f=%+.4f", -dsp->thd->xlt_fq);
        if (dsp->opt_dc) fprintf(stdout, "%+.6f", dsp->Df/(double)dsp->sr);
        fprintf(stdout, ">  ");
        ret = print_position(gpx, ec);
        if (ret==0) fprintf(stdout, "\n");
        pthread_mutex_unlock( dsp->thd->mutex );
    }
}

/* -------------------------------------------------------------------------- */


void *thd_rs41(void *targs) { // pcm_t *pcm, double xlt_fq

    thargs_t *tharg = targs;
    pcm_t *pcm = &(tharg->pcm);


    //int option_inv = 0;    // invertiert Signal
    int option_iq = 5; // baseband, decimate
    int option_ofs = 0;


    int k;

    char bitbuf[8];
    int bitpos = 0,
        b8pos = 0,
        byte_count = FRAMESTART;
    int bit, byte;
    int bitQ = 0;

    int header_found = 0;

    float thres = 0.7; // dsp.mv threshold
    float _mv = 0.0;

    int symlen = 1;
    int bitofs = 2; // +0 .. +3
    int shift = 0;

    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));

    gpx_t gpx = {0};

/*
#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _fileno(stdin)
#endif
    setbuf(stdout, NULL);
*/

    // init gpx

    gpx.option.vbs = 1;
    gpx.option.ptu = 2;
    gpx.option.aut = 1;
    gpx.option.jsn = tharg->option_jsn;

    gpx.option.ecc = 1;

    if (gpx.option.ecc) {
        rs_init_RS255(&gpx.RS);  // RS, GF
    }

    memcpy(gpx.frame, rs41_header_bytes, sizeof(rs41_header_bytes)); // 8 header bytes

    gpx.jsn_freq = tharg->jsn_freq;


    pcm->sel_ch = 0;

    // rs41: BT=0.5, h=0.8,1.0 ?
    symlen = 1;

    // init dsp
    //
    dsp.fp = pcm->fp;
    dsp.sr = pcm->sr;
    dsp.sr_base = pcm->sr_base;
    dsp.dectaps = pcm->dectaps;
    dsp.decM = pcm->decM;

    dsp.thd = &(tharg->thd);

    dsp.bps = pcm->bps;
    dsp.nch = pcm->nch;
    dsp.ch = pcm->sel_ch;
    dsp.br = (float)BAUD_RATE;
    dsp.sps = (float)dsp.sr/dsp.br;
    dsp.symlen = symlen;
    dsp.symhd  = symlen;
    dsp._spb = dsp.sps*symlen;
    dsp.hdr = rs41_header;
    dsp.hdrlen = strlen(rs41_header);
    dsp.BT = 0.5; // bw/time (ISI) // 0.3..0.5
    dsp.h = 0.6; //0.7;  // 0.7..0.8? modulation index abzgl. BT
    dsp.opt_iq = option_iq;
    dsp.opt_lp = 1;
    dsp.lpIQ_bw = 8e3; // IF lowpass bandwidth
    dsp.lpFM_bw = 6e3; // FM audio lowpass
    dsp.opt_dc  = tharg->option_dc;
    dsp.opt_cnt = tharg->option_cnt;

    if ( dsp.sps < 8 ) {
        //fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
    }


    k = init_buffers(&dsp); // BT=0.5  (IQ-Int: BT > 0.5 ?)
    if ( k < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        goto exit_thread;
    };

    //if (option_iq: 2,3) bitofs += 1; // FM: +1 , IQ: +2, IQ5: +1
    bitofs += shift;


    bitQ = 0;
    while ( 1 && bitQ != EOF )
    {
        header_found = find_header(&dsp, thres, 3, bitofs, dsp.opt_dc);
        _mv = dsp.mv;

        if (header_found == EOF) break;

        // mv == correlation score
        if (_mv *(0.5-gpx.option.inv) < 0) {
            if (gpx.option.aut == 0) header_found = 0;
            else gpx.option.inv ^= 0x1;
        }

        if (header_found)
        {
            byte_count = FRAMESTART;
            bitpos = 0; // byte_count*8-HEADLEN
            b8pos = 0;

            while ( byte_count < FRAME_LEN )
            {
                if (option_iq >= 2) {
                    float bl = -1;
                    if (option_iq > 2) bl = 1.0;
                    bitQ = read_slbit(&dsp, &bit, 0/*gpx.option.inv*/, bitofs, bitpos, bl, 0);
                }
                else {
                    bitQ = read_slbit(&dsp, &bit, 0/*gpx.option.inv*/, bitofs, bitpos, -1, 0);
                }
                if ( bitQ == EOF ) break; // liest 2x EOF, wenn nicht nochmal break

                if (gpx.option.inv) bit ^= 1;

                bitpos += 1;
                bitbuf[b8pos] = bit;
                b8pos++;
                if (b8pos == BITS) {
                    b8pos = 0;
                    byte = bits2byte(bitbuf);
                    gpx.frame[byte_count] = byte ^ mask[byte_count % MASK_LEN];
                    byte_count++;
                }
            }

            print_frame(&gpx, byte_count, &dsp);
            byte_count = FRAMESTART;
            header_found = 0;
        }
    }

    free_buffers(&dsp);

exit_thread:
    reset_blockread(&dsp);
    (dsp.thd)->used = 0;

    return NULL;
}

