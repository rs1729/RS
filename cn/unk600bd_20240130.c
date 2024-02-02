
/*
 *  (2024-01-30 12Z 403.5MHz)
 *  unknown 600 baud CF06/HT03-like radiosonde/pilotsonde ?
 *  (reversed bit order , byte-order: CF06 , time: HT03)
 *
 */

#include <stdio.h>
#include <string.h>

#include <math.h>
#include <stdlib.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


typedef unsigned char ui8_t;
typedef unsigned int ui32_t;
typedef unsigned short ui16_t;
typedef short i16_t;
typedef int i32_t;


#define BITFRAME_LEN    ((4+3+44+2+4)*8)
#define FRAMESTART      (HEADOFS+HEADLEN)

#define FRAME_LEN       (BITFRAME_LEN/8)

typedef struct {
    int frnr;
    //int week; int gpssec;
    //int jahr; int monat; int tag;
    int std; int min; float sek;
    float lat; float lon; float alt;
    float vH; float vD; float vV;
    ui8_t frame[FRAME_LEN+16];
    char sonde_id[16];
} gpx_t;


static int bits_ofs = 0;
#define HEADLEN 56
#define HEADOFS  0

//Preamble
static char header[] = "10101010""10101010""10101010""10101010"  // AA AA AA AA
                       "00101101""11010100""00101110"; // 2D D4 2E


static char buf[HEADLEN+1] = "xxxxxxxxxx\0";
static int bufpos = -1;

static char frame_bits[BITFRAME_LEN+4];


static int option_verbose = 0,  // ausfuehrliche Anzeige
           option_raw = 0,      // rohe Frames
           option_inv = 0,      // invertiert Signal
           option_auto = 0,
           option_avg = 0,      // moving average
           option_b = 0,
           option_ecc = 0,
           option_ptu = 0,
           wavloaded = 0;
static int wav_channel = 0;     // audio channel: left


static int start = 0;

/* -------------------------------------------------------------------------- */


// option_b: exakte Baudrate wichtig!
// eventuell in header ermittelbar
#define BAUD_RATE   599

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

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    samples_per_bit = sample_rate/(float)BAUD_RATE;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}


static unsigned long sample_count = 0;

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
static double bitgrenze = 0;
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


/* -------------------------------------------------------------------------- */


static void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

static char cb_inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

// Gefahr bei Manchester-Codierung: inverser Header wird leicht fehl-erkannt
// da manchester1 und manchester2 nur um 1 bit verschoben
static int compare2() {
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

    if (option_auto) {
    i = 0;
    j = bufpos;
    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != cb_inv(header[HEADOFS+HEADLEN-1-i])) break;
        j--;
        i++;
    }
    if (i == HEADLEN) return -1;
    }

    return 0;

}


static int bits2bytes(char *bitstr, ui8_t *bytes, int len) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < len) {

        byteval = 0;
        d = 1;
        for (i = 0; i < 8; i++) {
            //bit = *(bitstr+bitpos+i); /* little endian */
            bit = *(bitstr+bitpos+7-i);  /* big endian */
            if (bit == '\0') goto frame_end;
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += 8;
        bytes[bytepos++] = byteval;

    }
frame_end:
    for (i = bytepos; i < FRAME_LEN; i++) bytes[i] = 0;

    return bytepos;
}


/* -------------------------------------------------------------------------- */

static ui32_t u4(ui8_t *bytes) {  // 32bit unsigned int
    //ui32_t val = 0; // le: p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)
    //memcpy(&val, bytes, 4);
    return  bytes[0] | (bytes[1]<<8) | (bytes[2]<<16) | (bytes[3]<<24);
}

static i32_t i4(ui8_t *bytes) {  // 32bit signed int
    //i32_t val = 0; // le: p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)
    //memcpy(&val, bytes, 4);
    return  bytes[0] | (bytes[1]<<8) | (bytes[2]<<16) | (bytes[3]<<24);
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

static ui16_t u2(ui8_t *bytes) {  // 16bit unsigned int
    return  bytes[0] | (bytes[1]<<8);
}

static i16_t i2(ui8_t *bytes) { // 16bit signed int
    //return (i16_t)u2(bytes);
    //int val = bytes[0] | (bytes[1]<<8);
    //if (val & 0x8000) val -= 0x10000;
    i16_t val = bytes[0] | (bytes[1]<<8);
    return val;
}

static ui16_t u2be(ui8_t *bytes) {  // 16bit unsigned int
    return  bytes[1] | (bytes[0]<<8);
}

// -----------------------------------------------------------------------------

#define PREAMBLE         4
#define OFS              3
#define pos_TIME   (OFS   )  //   4 byte
#define pos_LON    (OFS+ 4)  //   4 byte
#define pos_LAT    (OFS+ 8)  //   4 byte
#define pos_ALT    (OFS+12)  //   4 byte
#define pos_VEL    (OFS+16)  // 3*2 byte
#define pos_CNT    (OFS+38)  //   2 byte
#define pos_CRC    (OFS+44)  //   2 byte

// -----------------------------------------------------------------------------

static int crc16(ui8_t bytes[], int len) {
    int crc16poly = 0x1021;
    int rem = 0x0000; // init value
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


// -----------------------------------------------------------------------------


static void print_frame(gpx_t *gpx, int pos) {
    int j;
    int crcOK = 0;


    if (option_raw == 2) {
        for (j = 0; j < pos; j++) {
            printf("%c", frame_bits[j]);
        }
        {
            printf("\n");
        }
    }
    else {
        int frmlen = (pos-bits_ofs)/8;
        bits2bytes(frame_bits+bits_ofs, gpx->frame, frmlen);

        ui8_t *frm = gpx->frame+PREAMBLE;

        // CRC
        ui32_t crcdat = u2be(frm+pos_CRC);
        ui32_t crcval = crc16(frm+OFS, pos_CRC-OFS); // pos_CRC-OFS = 44
        ui32_t crc_ok = (crcdat == crcval);

        if (option_raw == 1) {
            if (option_verbose == 2) printf(" :%6.1f: ", sample_count/(double)sample_rate);
            for (j = 0; j < frmlen; j++) {
                if (j == PREAMBLE          ||
                    j == PREAMBLE+OFS      ||
                    j == PREAMBLE+pos_CRC  ||
                    j == PREAMBLE+pos_CRC+2  ) printf(" ");
                printf("%02X ", gpx->frame[j]);
            }
            printf(" %s", crc_ok ? "[OK]" : "[NO]");
            printf("\n");
        }
        else {

            if (option_verbose == 2) printf(" :%6.1f: ", sample_count/(double)sample_rate);

            int n = OFS;
            ui32_t d = 0;
            float *f = (float*)&d;


            //d = frm[n] | (frm[n+1]<<8) | (frm[n+2]<<16) | (frm[n+3]<<24);
            //d = u4(frm+OFS);
            d = u4(frm+pos_TIME);

            ui32_t std, min, ms;
            float sek = 0.0f;

            // HT03 time format

            std =  frm[n]   & 0x1F;
            min =  frm[n+1] & 0x3F;
            sek = (frm[n+2] | (frm[n+1] & 0xC0) << 2) / 10.0f;
            printf(" %02d:%02d:%04.1f ", std, min, sek);  // UTC ?

            printf(" ");


            // position

            d = u4(frm+pos_LON);
            //memcpy(&f32, &d, 4);
            float lon = *f * 180.0/M_PI;
            printf(" lon: %.5f ", lon);

            d = u4(frm+pos_LAT);
            //memcpy(&f32, &d, 4);
            float lat = *f * 180.0/M_PI;
            printf(" lat: %.5f ", lat);

            d = u4(frm+pos_ALT);
            float alt = d/10.0f;
            printf(" alt: %.1f  ", alt);


            // velocity

            d = u2(frm+pos_VEL);
            float vN = ((short)d)/100.0f;

            d = u2(frm+pos_VEL+2);
            float vE = ((short)d)/100.0f;

            float vH = sqrt(vN*vN+vE*vE);
            float vD = atan2(vE, vN) * 180.0 / M_PI;
            if (vD < 0) vD += 360.0f;

            d = u2(frm+pos_VEL+4);
            float vV = ((short)d)/100.0f;

            printf(" vH:%.2f  D:%.1f  vV:%.2f  ", vH, vD, vV);


            int cnt = u2(frm+pos_CNT);
            printf(" [%4d]  ", cnt);


            if (option_verbose) printf(" [%04X:%04X] ", crcdat, crcval);
            printf(" %s", crc_ok ? "[OK]" : "[NO]");

            printf("\n");
        }
    }

}

/* -------------------------------------------------------------------------- */


int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;
    int pos, i, j, bit, len;
    int header_found = 0;

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
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       -b           (alt. Demod.)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "--ofs") == 0) ) {
            ++argv;
            if (*argv) {
                bits_ofs = atoi(*argv);
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-vv") == 0) ) {
            option_verbose = 2;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 0x1;
        }
        else if ( (strcmp(*argv, "--auto") == 0) ) {
            option_auto = 1;
        }
        else if ( (strcmp(*argv, "--avg") == 0) ) {
            option_avg = 1;
        }
        else if   (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        else if   (strcmp(*argv, "-b2") == 0) { option_b = 2; }
        else if ( (strcmp(*argv, "--ecc" ) == 0) ) { option_ecc = 1; }
        else if ( (strcmp(*argv, "--ptu") == 0) ) {
            option_ptu = 1;
        }
        else if   (strcmp(*argv, "--ch2") == 0) { wav_channel = 1; }  // right channel (default: 0=left)
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error open %s\n", *argv);
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

    memcpy(frame_bits, header, HEADLEN);

    pos = FRAMESTART;

    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) { // reset_frame();
            if (pos > BITFRAME_LEN-10) {
                print_frame(&gpx, pos);
                //header_found = 0;
                //pos = FRAMESTART;
            }
            header_found = 0;
            pos = FRAMESTART;
            inc_bufpos();
            buf[bufpos] = 'x';
            continue;   // ...
        }

        for (i = 0; i < len; i++) {

            inc_bufpos();
            buf[bufpos] = 0x30 + bit;  // Ascii

            if (!header_found) {
                header_found = compare2();
                if (header_found < 0) option_inv ^= 0x1;
            }
            else {
                frame_bits[pos] = 0x30 + bit;  // Ascii
                pos++;

                if (pos == BITFRAME_LEN) {
                    frame_bits[pos] = '\0';
                    print_frame(&gpx, pos);//FRAME_LEN
                    header_found = 0;
                    pos = FRAMESTART;
                }
            }

        }
        if (header_found && option_b==1) {
            bitstart = 1;

            while ( pos < BITFRAME_LEN ) {
                if (read_rawbit(fp, &bit) == EOF) break;
                frame_bits[pos] = 0x30 + bit;
                pos++;
            }
            frame_bits[pos] = '\0';
            print_frame(&gpx, pos);//FRAME_LEN

            header_found = 0;
            pos = FRAMESTART;
        }
    }


    fclose(fp);

    return 0;
}

