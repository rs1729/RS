
/*
 * pilotsonde m12
 * FSK, 4800 baud, 8N1, little endian
 *
 *  compile:
 *      gcc -c demod_mod.c
 *      gcc m12mod.c demod_mod.o -lm -o m12mod
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


#include "demod_mod.h"


typedef struct {
    i8_t vbs;  // verbose output
    i8_t raw;  // raw frames
    i8_t crc;  // CRC check output
    i8_t sat;  // GPS sat data
    i8_t ptu;  // PTU: temperature
    i8_t inv;
    i8_t aut;
} option_t;


#define BAUD_RATE   4800

/* -------------------------------------------------------------------------- */

#define BITS (1+8+1)
#define HEADLEN 20  // HEADLEN+HEADOFS=32 <= strlen(bitheader)
#define HEADOFS  0

                         //  A   A       A   A       A   A     : preamble: 0xAAAAAA
static char m12_header[] = "0010101011""0010101011""0010101011";
static ui8_t m12_header_bytes[3] = { 0xAA, 0xAA, 0xAA};

#define FRAME_LEN       (50)
#define BITFRAME_LEN    (FRAME_LEN*BITS)

typedef struct {
    int lat; int lon; int alt;
    int vE; int vN; int vU;
    double vH; double vD; double vV;
    int date; int time;
    ui8_t sats; ui8_t fix;
    ui8_t frame_bytes[FRAME_LEN+10];
    char frame_bits[BITFRAME_LEN+4];
    option_t option;
} gpx_t;


/* -------------------------------------------------------------------------- */

static int bits2bytes(gpx_t *gpx, char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN) {

        byteval = 0;
        d = 1;
        for (i = 1; i < BITS-1; i++) {
            bit=*(bitstr+bitpos+i); /* little endian */
            //bit=*(bitstr+bitpos+7-i);  /* big endian */
            if         (bit == '1')                     byteval += d;
            else /*if ((bit == '0') || (bit == 'x'))*/  byteval += 0;
            d <<= 1;
        }
        byteval &= 0xFF;
        bitpos += BITS;

        if (bytepos == 0 && byteval == gpx->frame_bytes[2]/*0xAA*/) continue;

        bytes[bytepos++] = byteval & 0xFF;

    }

    //while (bytepos < FRAME_LEN+) bytes[bytepos++] = 0;

    return 0;
}

/* -------------------------------------------------------------------------- */


#define OFS           (0x03)
#define pos_GPSlat    (OFS+0x03)  // 4 byte
#define pos_GPSlon    (OFS+0x07)  // 4 byte
#define pos_GPSalt    (OFS+0x0B)  // 4 byte
#define pos_GPSvE     (OFS+0x0F)  // 2 byte
#define pos_GPSvN     (OFS+0x11)  // 2 byte
#define pos_GPSvU     (OFS+0x13)  // 2 byte
#define pos_GPStime   (OFS+0x15)  // 4 byte
#define pos_GPSdate   (OFS+0x19)  // 4 byte
#define pos_GPSsats   (OFS+0x01)  // 1 byte
#define pos_GPSfix    (OFS+0x02)  // 1 byte


static int get_GPStime(gpx_t *gpx) {
    int i;
    int gpstime;

    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpx->frame_bytes[pos_GPStime + i] << (8*(3-i));
    }

    gpx->time = gpstime;

    return 0;
}

static int get_GPSpos(gpx_t *gpx) {
    int i;
    int val;

    val = 0;
    for (i = 0; i < 4; i++)  val |= gpx->frame_bytes[pos_GPSlat + i] << (8*(3-i));
    gpx->lat = val;

    val = 0;
    for (i = 0; i < 4; i++)  val |= gpx->frame_bytes[pos_GPSlon + i] << (8*(3-i));
    gpx->lon = val;

    val = 0;
    for (i = 0; i < 4; i++)  val |= gpx->frame_bytes[pos_GPSalt + i] << (8*(3-i));
    gpx->alt = val;

    return 0;
}

static int get_GPSvel(gpx_t *gpx) {
    int i;
    ui8_t bytes[2];
    short vel16;
    double vx, vy, vz, dir;

    for (i = 0; i < 2; i++)  bytes[i] = gpx->frame_bytes[pos_GPSvE + i];
    vel16 = bytes[0] << 8 | bytes[1];
    gpx->vE = vel16;
    vx = vel16 / 1e2; // east

    for (i = 0; i < 2; i++)  bytes[i] = gpx->frame_bytes[pos_GPSvN + i];
    vel16 = bytes[0] << 8 | bytes[1];
    gpx->vN = vel16;
    vy= vel16 / 1e2; // north

    for (i = 0; i < 2; i++)  bytes[i] = gpx->frame_bytes[pos_GPSvU + i];
    vel16 = bytes[0] << 8 | bytes[1];
    gpx->vU = vel16;
    vz = vel16 / 1e2; // up

    gpx->vH = sqrt(vx*vx+vy*vy);
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;
    gpx->vV = vz;

    return 0;
}

static int get_GPSdate(gpx_t *gpx) {
    int i;
    int val;

    val = 0;
    for (i = 0; i < 4; i++)  val |= gpx->frame_bytes[pos_GPSdate + i] << (8*(3-i));
    gpx->date = val;

    return 0;
}

static int get_GPSstatus(gpx_t *gpx) {
    gpx->sats = gpx->frame_bytes[pos_GPSsats];
    gpx->fix  = gpx->frame_bytes[pos_GPSfix];
    return 0;
}

/* -------------------------------------------------------------------------- */

static int crc16poly = 0xA001;

static unsigned int crc16rev(unsigned char bytes[], int len) {
    unsigned int rem = 0xFFFF; // init value: crc(0xAAAAAA;init:0xFFFF)=0x3FAF
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

/* -------------------------------------------------------------------------- */

static int print_pos(gpx_t *gpx) {
    int err;
    unsigned int crc = 0x0000;

    err = 0;
    err |= get_GPSpos(gpx);
    err |= get_GPSvel(gpx);
    err |= get_GPStime(gpx);
    err |= get_GPSdate(gpx);

    if (!err) {
        //fprintf(stdout, " (%06d)", gpx->date);
        fprintf(stdout, " %02d-%02d-%02d", gpx->date/10000, (gpx->date%10000)/100, gpx->date%100);
        //fprintf(stdout, " (%09d)", gpx->time);
        fprintf(stdout, " %02d:%02d:%06.3f ", gpx->time/10000000, (gpx->time%10000000)/100000, (gpx->time%100000)/1000.0);

        fprintf(stdout, " lat: %.6f° ", gpx->lat/1e6);
        fprintf(stdout, " lon: %.6f° ", gpx->lon/1e6);
        fprintf(stdout, " alt: %.2fm ", gpx->alt/1e2);

        //fprintf(stdout, "  (%.1f , %.1f , %.1f) ", gpx->vE/1e2, gpx->vN/1e2, gpx->vU/1e2);
        fprintf(stdout, "  vH: %.1fm/s  D: %.1f°  vV: %.1fm/s", gpx->vH, gpx->vD, gpx->vV);

        if (gpx->option.vbs) {
            get_GPSstatus(gpx);
            fprintf(stdout, "  sats: %d  fix: %d ", gpx->sats, gpx->fix);
        }

        crc = (gpx->frame_bytes[FRAME_LEN-2]<<8) | gpx->frame_bytes[FRAME_LEN-1];
        fprintf(stdout, "  # CRC ");
        if (crc == crc16rev(gpx->frame_bytes, FRAME_LEN-2)) fprintf(stdout, "[OK]"); else fprintf(stdout, "[NO]");
    }
    fprintf(stdout, "\n");

    return err;
}

static int print_frame(gpx_t *gpx, int pos) {
    int i;
    unsigned int crc = 0x0000;
    int crc_pos = FRAME_LEN-2;

    bits2bytes(gpx, gpx->frame_bits, gpx->frame_bytes+OFS);

    if (gpx->option.raw) {
        if (gpx->option.raw == 2) {
            for (i = 0; i < BITFRAME_LEN; i++) {
                fprintf(stdout, "%c", gpx->frame_bits[i]);
                if (i%10==0 || i%10==8) fprintf(stdout, " ");
            }
            fprintf(stdout, "\n");
        }
        else {
            for (i = OFS; i < crc_pos; i++) {
                fprintf(stdout, "%02x ", gpx->frame_bytes[i]);
            }
            crc = (gpx->frame_bytes[crc_pos]<<8) | gpx->frame_bytes[crc_pos+1];
            fprintf(stdout, " %04x ", crc);
            fprintf(stdout, "# %04x ", crc16rev(gpx->frame_bytes, crc_pos));
            fprintf(stdout, "\n");
        }
    }

    else print_pos(gpx);

    return 0;
}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    int option_verbose = 0;  // ausfuehrliche Anzeige
    int option_raw = 0;      // rohe Frames
    int option_inv = 0;      // invertiert Signal
    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;     // audio channel: left
    int spike = 0;

    float baudrate = -1;

    FILE *fp = NULL;
    char *fpname = NULL;

    int k;

    int bit, bit0;
    int bitpos = 0;
    int bitQ;
    int pos;
    hsbit_t hsbit, hsbit1;

    //int headerlen = 0;

    int header_found = 0;

    float thres = 0.76;
    float _mv = 0.0;

    int symlen = 1;
    int bitofs = 0; // 0 .. +2
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));

    hdb_t hdb = {0};

    gpx_t gpx = {0};


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if   (strcmp(*argv, "--auto") == 0) { gpx.option.aut = 1; }
        else if ( (strcmp(*argv, "--br") == 0) ) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);
                if (baudrate < 4000 || baudrate > 5000) baudrate = BAUD_RATE; // default: 4800
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--spike") == 0) ) {
            spike = 1;
        }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { sel_wavch = 1; }  // right channel (default: 0=left)
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }  // float32 soft input
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }  // float32 inverted soft input
        else if ( (strcmp(*argv, "--ths") == 0) ) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "-d") == 0) ) {
            ++argv;
            if (*argv) {
                shift = atoi(*argv);
                if (shift >  4) shift =  4;
                if (shift < -4) shift = -4;
            }
            else return -1;
        }
        else if   (strcmp(*argv, "--iq0") == 0) { option_iq = 1; }  // differential/FM-demod
        else if   (strcmp(*argv, "--iq2") == 0) { option_iq = 2; }
        else if   (strcmp(*argv, "--iq3") == 0) { option_iq = 3; }  // iq2==iq3
        else if   (strcmp(*argv, "--iqdc") == 0) { option_iqdc = 1; }  // iq-dc removal (iq0,2,3)
        else if   (strcmp(*argv, "--IQ") == 0) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                     // --IQ <fq> , -0.5 < fq < 0.5
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
            option_iq = 5;
        }
        else if   (strcmp(*argv, "--lp") == 0) { option_lp = 1; }  // IQ lowpass
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if (strcmp(*argv, "-") == 0) {
            int sample_rate = 0, bits_sample = 0, channels = 0;
            ++argv;
            if (*argv) sample_rate = atoi(*argv); else return -1;
            ++argv;
            if (*argv) bits_sample = atoi(*argv); else return -1;
            channels = 2;
            if (sample_rate < 1 || (bits_sample != 8 && bits_sample != 16 && bits_sample != 32)) {
                fprintf(stderr, "- <sr> <bs>\n");
                return -1;
            }
            pcm.sr  = sample_rate;
            pcm.bps = bits_sample;
            pcm.nch = channels;
            option_pcmraw = 1;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error: open %s\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;


    // init gpx
    gpx.option.inv = option_inv; // irrelevant
    gpx.option.vbs = option_verbose;
    gpx.option.raw = option_raw;

    memcpy(gpx.frame_bytes, m12_header_bytes, sizeof(m12_header_bytes));


    #ifdef EXT_FSK
    if (!option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

    if (!option_softin) {

        if (option_iq == 0 && option_pcmraw) {
            fclose(fp);
            fprintf(stderr, "error: raw data not IQ\n");
            return -1;
        }
        if (option_iq) sel_wavch = 0;

        pcm.sel_ch = sel_wavch;
        if (option_pcmraw == 0) {
            k = read_wav_header(&pcm, fp);
            if ( k < 0 ) {
                fclose(fp);
                fprintf(stderr, "error: wav header\n");
                return -1;
            }
        }


        symlen = 1;

        // init dsp
        //
        dsp.fp = fp;
        dsp.sr = pcm.sr;
        dsp.bps = pcm.bps;
        dsp.nch = pcm.nch;
        dsp.ch = pcm.sel_ch;
        dsp.br = (float)BAUD_RATE;
        dsp.sps = (float)dsp.sr/dsp.br;
        dsp.symlen = symlen;
        dsp.symhd = 1; // M12!header
        dsp._spb = dsp.sps*symlen;
        dsp.hdr = m12_header;
        dsp.hdrlen = strlen(m12_header);
        dsp.BT = 1.8; // bw/time (ISI) // 1.0..2.0  // like M10 ?
        dsp.h = 0.9;  // 1.2 modulation index       // like M10 ?
        dsp.opt_iq = option_iq;
        dsp.opt_iqdc = option_iqdc;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = 20e3; // IF lowpass bandwidth
        dsp.lpFM_bw = 6e3; // FM audio lowpass
        dsp.opt_dc = option_dc;
        dsp.opt_IFmin = option_min;

        if ( dsp.sps < 8 ) {
            fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
        }

        if (baudrate > 0) {
            dsp.br = (float)baudrate;
            dsp.sps = (float)dsp.sr/dsp.br;
            fprintf(stderr, "sps corr: %.4f\n", dsp.sps);
        }

        //m12_headerlen = dsp.hdrlen;


        k = init_buffers(&dsp);
        if ( k < 0 ) {
            fprintf(stderr, "error: init buffers\n");
            return -1;
        }

        bitofs += shift;
    }
    else {
        // init circular header bit buffer
        hdb.hdr = m12_header;
        hdb.len = strlen(m12_header);
        //hdb.thb = 1.0 - 3.1/(float)hdb.len; // 1.0-max_bit_errors/hdrlen
        hdb.bufpos = -1;
        hdb.buf = NULL;
        /*
        calloc(hdb.len, sizeof(char));
        if (hdb.buf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
        */
        hdb.ths = 0.8; // caution 0.7: false positive / offset
        hdb.sbuf = calloc(hdb.len, sizeof(float));
        if (hdb.sbuf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
    }


    while ( 1 )
    {
        if (option_softin) {
            header_found = find_softbinhead(fp, &hdb, &_mv, option_softin == 2);
        }
        else {                                                              // FM-audio:
            header_found = find_header(&dsp, thres, 2, bitofs, dsp.opt_dc); // optional 2nd pass: dc=0
            _mv = dsp.mv;
        }

        if (header_found == EOF) break;

        // mv == correlation score
        if (_mv*(0.5-gpx.option.inv) < 0) {
            if (gpx.option.aut == 0) header_found = 0;
            else gpx.option.inv ^= 0x1;
        }

        if (header_found) {

            bitpos = 0;
            pos = 0;

            while ( pos < BITFRAME_LEN ) {

                if (option_softin) {
                    float s1 = 0.0;
                    float s2 = 0.0;
                    float s = 0.0;
                    bitQ = f32soft_read(fp, &s1, option_softin == 2);
                    if (bitQ != EOF) {
                        bitQ = f32soft_read(fp, &s2, option_softin == 2);
                        if (bitQ != EOF) {
                            s = s2-s1; // integrate both symbols  // only 2nd Manchester symbol: s2
                            bit = (s>=0.0); // no soft decoding
                        }
                    }
                }
                else {
                    float bl = -1;
                    if (option_iq >= 2) spike = 0;
                    if (option_iq > 2)  bl = 4.0;
                    //bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, bl, spike); // symlen=1
                    bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, spike, &hsbit1); // symlen=1
                    bit = hsbit.hb;
                }
                if ( bitQ == EOF ) { break; }

                if (gpx.option.inv) bit ^= 1;

                gpx.frame_bits[pos] = 0x30 ^ bit;
                pos++;
                bitpos += 1;
            }
            gpx.frame_bits[pos] = '\0';
            print_frame(&gpx, pos);
            if (pos < BITFRAME_LEN) break;

            header_found = 0;
            /*
            // bis Ende der Sekunde vorspulen; allerdings Doppel-Frame alle 10 sek
            if (gpx.option.vbs < 3) { // && (regulare frame) // print_frame-return?
                while ( bitpos < 2*BITFRAME_LEN ) {
                    bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, -1, 0); // symlen=1
                    if ( bitQ == EOF) break;
                    bitpos++;
                }
            }
            */
            pos = 0;
        }
    }


    if (!option_softin) free_buffers(&dsp);
    else {
        if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
    }

    fclose(fp);

    return 0;
}

