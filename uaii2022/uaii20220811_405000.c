
/*
    UAII2022 Lindenberg
    2022-08-11 13:30Z
    405000 kHz
    Vikram PS-B3 ?
*/

#include <stdio.h>
#include <string.h>


typedef unsigned char ui8_t;
typedef short i16_t;
typedef unsigned int ui32_t;


int option_raw = 0,
    option_inv = 0,
    option_b = 0,
    option_timestamp = 0,
    wavloaded = 0;
int wav_channel = 0;     // audio channel: left


#define BAUD_RATE   770

#define HEADLEN 32
#define HEADOFS 0
char header[] = "10101010100101101001100101100101";

char buf[HEADLEN+1] = "xxxxxxxxxx\0";
int bufpos = 0;


#define FRAMELEN       (48)
#define BITFRAMELEN    (FRAMELEN*8)
#define RAWBITFRAMELEN (2*BITFRAMELEN)

char rawbits[RAWBITFRAMELEN+128];
char bits[BITFRAMELEN+64];
ui8_t frame[FRAMELEN+8];

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
    int n, y0;
    float l, x1;
    static float x0;

    n = 0;
    do {
        y0 = sample;
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

// manchester1 1->10,0->01: 1.bit
// manchester2 0->10,1->01: 2.bit
void manchester1(char* frame_rawbits, char *frame_bits) {
    int i;
    char bit, bits[2];

    for (i = 0; i < BITFRAMELEN; i++) {
        bits[0] = frame_rawbits[2*i];
        bits[1] = frame_rawbits[2*i+1];

        if ((bits[0] == '0') && (bits[1] == '1')) bit = '0';
        else
        if ((bits[0] == '1') && (bits[1] == '0')) bit = '1';
        else bit = 'x';
        frame_bits[i] = bit;
    }
}
void manchester2(char* frame_rawbits, char *frame_bits) {
    int i;
    char bit, bits[2];

    for (i = 0; i < BITFRAMELEN; i++) {
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

int dechex(ui8_t b) { // sign ?
    return ((b>>4) & 0xF)*10 + (b & 0xF);
}

int print_frame() {

    manchester1(rawbits, bits);
    bits2bytes(bits, frame);

    if (option_raw) {
        if (option_raw == 1) {
            for (int j = 0; j < FRAMELEN; j++) {
                printf("%02X ", frame[j]);
            }
        }
        else {
            printf("%s", bits);
            /*
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", bits[j]);
                if (j % 8 == 7) printf(" ");
            }
            */
        }
        printf("\n");
    }
    else
    {
        ui32_t count;
        count = (frame[4]<<8) | frame[5];
        printf("[%5d] ", count);

        // date?
        printf(" 20%02X-__-%02X ", frame[22], frame[20]);

        ui8_t hrs = dechex(frame[23]);
        ui8_t min = dechex(frame[24]);
        ui8_t sec = dechex(frame[25]);
        printf(" %02X:%02X:%02X ", frame[23], frame[24], frame[25]);  // UTC

        int   lat_deg = dechex(frame[26]);  // sign ?
        ui8_t lat_min1 = dechex(frame[27]);
        ui8_t lat_min2 = dechex(frame[28]);
        ui8_t lat_min3 = dechex(frame[29]);
        float lat = lat_deg + (lat_min1 + lat_min2*1e-2f + lat_min3*1e-4f) / 60.0f;
        printf(" lat: %.4f ", lat);

        int   lon_deg = dechex(frame[30])*10 + dechex(frame[31]>>4);  // sign ?
        ui8_t lon_min1 = dechex( ((frame[31]&0xF)<<4) | ((frame[32]>>4)&0xF) );
        ui8_t lon_min2 = dechex( ((frame[32]&0xF)<<4) | ((frame[33]>>4)&0xF) );
        ui8_t lon_min3 = dechex( ((frame[33]&0xF)<<4) | ((frame[34]>>4)&0xF) );
        float lon = lon_deg +  (lon_min1 + lon_min2*1e-2f + lon_min3*1e-4f) / 60.0f;
        printf(" lon: %.4f ", lon);

        //frame[35]

        int   alt1 = dechex(frame[36]);
        ui8_t alt2 = dechex(frame[37]);
        ui8_t alt3 = dechex(frame[38]);
        float alt = (alt1*1e4f + alt2*1e2f + alt3) / 10.0f;  // MSL
        printf(" alt: %.1f ", alt);

        printf("\n");
    }

    return 0;
}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;

    int ofs, i, j, h, h2, bit, len, abw;
    int bit_count, frame_count;
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
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
        }
        else if   (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        else if   (strcmp(*argv, "-t" ) == 0) { option_timestamp = 1; }
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
    frame_count = 0;
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
                    strncpy(rawbits, header, HEADLEN);
                    bit_count += HEADLEN;
                    frame_count++;
                }
            }
            else
            {
                rawbits[bit_count] = 0x30 + bit;
                bit_count += 1;
            }

            if (bit_count >= RAWBITFRAMELEN) {
                bit_count = 0;
                header_found = 0;

                print_frame();
            }

        }
        if (header_found && option_b && 0) {
            bitstart = 1;

            while ( bit_count < RAWBITFRAMELEN ) {
                if (read_rawbit(fp, &bit) == EOF) break;
                rawbits[bit_count] = 0x30 + bit;
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

