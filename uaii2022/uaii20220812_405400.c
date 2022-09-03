
/*
    UAII2022 Lindenberg
    2022-08-12 12:58Z
    405400 kHz  (64kHz wide)
    Weathex WxR-301D ?
*/

#include <stdio.h>
#include <string.h>


typedef unsigned char ui8_t;
typedef short i16_t;
typedef unsigned int ui32_t;


int option_verbose = 0,
    option_raw = 0,
    option_inv = 0,
    option_b = 0,
    option_timestamp = 0,
    wavloaded = 0;
int wav_channel = 0;     // audio channel: left


#define BAUD_RATE   (4997.2) // 5000

#define FRAMELEN    64 //200 //(66)
#define BITFRAMELEN (8*FRAMELEN)

#define HEADLEN 56 //64
#define HEADOFS 0
char header[] = "10101010""10101010""10101010"//"10101010"  // preamble
                "11000001""10010100""11000001""11000110";
//preamble_header_sn1: 101010101010101010101010 1100000110010100110000011100011001111000 110001010110110111100100
//preamble_header_sn2: 101010101010101010101010 1100000110010100110000011100011001111000 001100100110110111100100
//preamble_header_sn3: 101010101010101010101010 1100000110010100110000011100011001111000 001010000110110111100100
char buf[HEADLEN+1] = "xxxxxxxxxx\0";
int bufpos = 0;

char frame_bits[BITFRAMELEN+1];
ui8_t frame_bytes[FRAMELEN+1];
ui8_t xframe[FRAMELEN+1];

int sample_rate = 0, bits_sample = 0, channels = 0;
float samples_per_bit = 0;

int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
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
double bitgrenze = 0;

int read_signed_sample(FILE *fp) {  // int = i32_t
    int byte, i, sample=0, s=0;     // EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
                           // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF_INT;
        if (i == wav_channel) sample = byte;

        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF_INT;
            if (i == wav_channel) sample +=  byte << 8;
        }

    }

    if (bits_sample ==  8)  s = sample-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16)  s = (short)sample;

    sample_count++;

    return s;
}

int par=1, par_alt=1;

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    static int sample;
    int n;
    float l;

    n = 0;
    do{
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
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

int bitstart = 0;
unsigned long scount = 0;
int read_rawbit(FILE *fp, int *bit) {
    int sample;
    int sum;

    sum = 0;

    if (bitstart) {
        scount = 0;    // eigentlich scount = 1
        bitgrenze = 0; //   oder bitgrenze = -1
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        //par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (option_inv) *bit ^= 1;

    return 0;
}

int compare() {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == header[HEADLEN+HEADOFS-1-i])) {
        i++;
    }
    return i;
}

char inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

int compare2() {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == inv(header[HEADLEN+HEADOFS-1-i]))) {
        i++;
    }
    return i;
}

int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAMELEN) {

        byteval = 0;
        d = 1;
        for (i = 0; i < 8; i++) {
            //bit = bitstr[bitpos+i]; /* little endian */
            bit = bitstr[bitpos+7-i];  /* big endian */
            if         (bit == '1')     byteval += d;
            else /*if ((bit == '0') */  byteval += 0;
            d <<= 1;
        }
        bitpos += 8;
        bytes[bytepos++] = byteval;

    }

    //while (bytepos < FRAME_LEN) bytes[bytepos++] = 0;

    return 0;
}


// PN9 Data Whitening
// cf. https://www.ti.com/lit/an/swra322/swra322.pdf
//     https://destevez.net/2019/07/lucky-7-decoded/
//
// counter low byte: frame[OFS+6] XOR 0xCC
// zero bytes, frame[OFS+32]: 0C CA C9 FB 49 37 E5 A8
//
ui8_t  PN9b[64] = { 0xFF, 0x87, 0xB8, 0x59, 0xB7, 0xA1, 0xCC, 0x24,
                    0x57, 0x5E, 0x4B, 0x9C, 0x0E, 0xE9, 0xEA, 0x50,
                    0x2A, 0xBE, 0xB4, 0x1B, 0xB6, 0xB0, 0x5D, 0xF1,
                    0xE6, 0x9A, 0xE3, 0x45, 0xFD, 0x2c, 0x53, 0x18,
                    0x0C, 0xCA, 0xC9, 0xFB, 0x49, 0x37, 0xE5, 0xA8,
                    0x51, 0x3B, 0x2F, 0x61, 0xAA, 0x72, 0x18, 0x84,
                    0x02, 0x23, 0x23, 0xAB, 0x63, 0x89, 0x51, 0xB3,
                    0xE7, 0x8B, 0x72, 0x90, 0x4C, 0xE8, 0xFb, 0xC1};


ui32_t xor8sum(ui8_t bytes[], int len) {
    int j;
    ui8_t xor8 = 0;
    ui8_t sum8 = 0;

    for (j = 8; j < 8+53; j++) {
        xor8 ^= xframe[j];
        sum8 += xframe[j];
    }
    //sum8 &= 0xFF;

    return  (xor8 << 8) | sum8;
}


#define OFS 6

int print_frame() {
    int j;
    int chkdat, chkval, chk_ok;

    bits2bytes(frame_bits, frame_bytes);

    for (j = 0; j < FRAMELEN; j++) {
        ui8_t b = frame_bytes[j];
        if (j >= 6) b ^= PN9b[(j-6)%64];
        xframe[j] = b;
    }

    chkval = xor8sum(xframe+8, 53);
    chkdat = (xframe[61]<<8) | xframe[61+1];
    chk_ok = (chkdat == chkval);

    if (option_raw) {
        if (option_raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                //printf("%02X ", frame_bytes[j]);
                printf("%02X ", xframe[j]);
            }
            printf(" #  %s", chk_ok ? "[OK]" : "[NO]");
            if (option_verbose) printf(" # [%04X:%04X]", chkdat, chkval);
        }
        else {
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", frame_bits[j]);
                //if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    else {

        ui32_t sn;
        ui32_t cnt;
        int val;

        // SN ?
        sn = xframe[OFS+2] | (xframe[OFS+3]<<8) | ((xframe[OFS+4])<<16) | ((xframe[OFS+5])<<24);
        printf(" (0x%08X) ", sn);

        // counter ?
        cnt = xframe[OFS+6] | (xframe[OFS+7]<<8);
        printf(" [%5d] ", cnt);

        ui8_t frid = xframe[OFS+8];

        if (frid == 2)
        {
            // time/UTC
            int hms;
            hms = xframe[OFS+9] | (xframe[OFS+10]<<8) | ((xframe[OFS+11])<<16);
            hms &= 0x3FFFF;
            //printf(" (%6d) ", hms);
            ui8_t h =  hms / 10000;
            ui8_t m = (hms % 10000) / 100;
            ui8_t s =  hms % 100;
            printf(" %02d:%02d:%02d ", h, m, s);  // UTC

            // alt
            val = xframe[OFS+15] | (xframe[OFS+16]<<8) | ((xframe[OFS+17])<<16);
            val &= 0x7FFFFF; // int23 ?
            val >>= 4;
            //if (val & 0x3FFFFF) val -= 0x400000;
            float alt = val / 10.0f;
            printf(" alt: %.1f ", alt);  // MSL

            // lat
            val = xframe[OFS+17] | (xframe[OFS+18]<<8) | ((xframe[OFS+19])<<16) | ((xframe[OFS+20])<<24);
            val >>= 7;
            val &= 0x3FFFFFF; // int26 ?
            if (val & 0x2000000) val -= 0x4000000; // sign ?
            float lat = val / 1e5f;
            printf(" lat: %.4f ", lat);

            // lon
            val = xframe[OFS+21] | (xframe[OFS+22]<<8) | ((xframe[OFS+23])<<16)| ((xframe[OFS+24])<<24);
            val &= 0x3FFFFFF; // int26 ?
            if (val & 0x2000000) val -= 0x4000000; // sign ?
            float lon = val / 1e5f;
            printf(" lon: %.4f ", lon);

        }

        // checksum
        printf("  %s", chk_ok ? "[OK]" : "[NO]");
        if (option_verbose) printf(" # [%04X:%04X]", chkdat, chkval);

        printf("\n");
    }

    return 0;
}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;

    int i, j, h, bit, len;
    int bit_count, frames;
    int header_found = 0;


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
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
    if (i) return -1;


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

