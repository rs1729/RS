
/*
    UAII2022 Lindenberg
    2022-08-12 12:58Z
    402600 kHz
    Aerospace Newsky CF-06AH ?
*/

#include <stdio.h>
#include <string.h>
#include <math.h>


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


#define BAUD_RATE   2400

#define FRAMELEN    107 //128
#define BITFRAMELEN (8*FRAMELEN)

#define HEADLEN 64
#define HEADOFS 0
char header[] = "01010101""01010101""01010101""01010101""01010101"  // preamble
                "10110100""00101011""11000110";
char buf[HEADLEN+1] = "xxxxxxxxxx\0";
int bufpos = 0;

char frame_bits[BITFRAMELEN+1];
ui8_t frame_bytes[FRAMELEN+1];

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


ui32_t crc16(ui8_t bytes[], int len) {
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


char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};


int print_frame() {
    int i, j;
    int crcdat1, crcval1, crc_ok1,
        crcdat2, crcval2, crc_ok2;

    for (j = 0; j < FRAMELEN; j++) {
        ui8_t byteval = 0;
        ui8_t d = 1;
        for (i = 0; i < 8; i++) { /* little endian */
            if (frame_bits[8*j+i] & 1) byteval += d;
            d <<= 1;
        }
        frame_bytes[j] = byteval;
    }

    // CRC_1
    crcdat1 = (frame_bytes[52]<<8) | frame_bytes[52+1];
    crcval1 = crc16(frame_bytes+12, 40);
    crc_ok1 = (crcdat1 == crcval1);
    // CRC_2
    crcdat2 = (frame_bytes[99]<<8) | frame_bytes[99+1];
    crcval2 = crc16(frame_bytes+70, 29) ^ 0x39BB; // crc16(0xA9BD)=0x39BB;
    crc_ok2 = (crcdat2 == crcval2);

    if (option_raw) {
        if (option_raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                printf("%02X ", frame_bytes[j]);
            }
            printf(" #  [%s,%s]", crc_ok1 ? "OK1" : "NO1", crc_ok2 ? "OK2" : "NO2");
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

        printf(" ("); // SN ?
        for (i = 0; i < 4; i++) printf("%02d", frame_bytes[12+i]);
        printf(")  ");

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[16+i] << (8*i);

        gpstime = val;

        ms = gpstime % 1000;
        gpstime /= 1000;

        wday = (gpstime / (24 * 3600)) % 7;
        if ((wday < 0) || (wday > 6)) wday = 0;

        gpstime %= (24*3600);

        std =  gpstime / 3600;
        min = (gpstime % 3600) / 60;
        sek =  gpstime % 60 + ms/1000.0;

        printf("%s ", weekday[wday]);

        printf("%02d:%02d:%06.3f ", std, min, sek);
        printf(" ");

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[20+i] << (8*i);
        ival = (int)val;
        float lon = ival / 1e7;
        printf(" lon: %.4f ", lon);

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[24+i] << (8*i);
        ival = (int)val;
        float lat = ival / 1e7;
        printf(" lat: %.4f ", lat);

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[28+i] << (8*i);
        ival = (int)val;
        float alt = ival / 1e3;
        printf(" alt: %.1f ", alt);  // MSL


        i16_t val16;
        val16 = 0;
        for (i = 0; i < 2; i++) val16 |= frame_bytes[32+i] << (8*i);
        float vE = val16/100.0f;
        val16 = 0;
        for (i = 0; i < 2; i++) val16 |= frame_bytes[34+i] << (8*i);
        float vN = val16/100.0f;
        val16 = 0;
        for (i = 0; i < 2; i++) val16 |= frame_bytes[36+i] << (8*i);
        float vU = -val16/100.0f;
        //printf(" (%.2f,%.2f,%.2f) ", vE, vN, vU);
        float vH = sqrt(vN*vN+vE*vE);
        float vD = atan2(vE, vN) * 180.0 / M_PI;
        if (vD < 0) vD += 360;
        printf("  vH: %.2f D: %.2f vV: %.2f ", vH, vD, vU);

        printf(" ");

        // CRC_1
        printf(" %s", crc_ok1 ? "[OK1]" : "[NO1]");
        if (option_verbose) printf(" # [%04X:%04X]", crcdat1, crcval1);
        // CRC_2
        if (option_verbose) {
            printf(" ");
            printf(" %s", crc_ok2 ? "[OK2]" : "[NO2]");
            if (option_verbose) printf(" # [%04X:%04X]", crcdat2, crcval2);
        }


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

