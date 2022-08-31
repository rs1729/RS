
/*
    Meteosis MTS01 ?
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


#define BAUD_RATE   1200

#define FRAMELEN    148
#define BITFRAMELEN (8*FRAMELEN)

#define HEADLEN 32 //40
#define HEADOFS 0
char header[] = "10101010""10101010""10101010"//"10101010"  // preamble
                "11010000";//10101110 00000000 11000100
char buf[HEADLEN+1] = "xxxxxxxxxx\0";
int bufpos = 0;

char frame_bits[BITFRAMELEN+80];
ui8_t frame_bytes[FRAMELEN+8];

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

int ofs = 6;

int print_frame() {
    int i, j;

    bits2bytes(frame_bits+ofs, frame_bytes);

    if (option_raw) {
        if (option_raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                printf("%02X ", frame_bytes[j]);
            }
        }
        else {
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", frame_bits[ofs+j]);
                if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    else {

        // ASCII-String:
        // SN/ID?,?,counter,YYMMDDhhmmss,?,lat,lon,alt?,?,?,?,?,?,?,?,?,?,?

        printf("%s\n", frame_bytes+5);
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

