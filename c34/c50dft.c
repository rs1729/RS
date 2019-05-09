
/*
   C50
   (empfohlen: sample rate 48kHz)
   gcc c50dft.c -lm -o c50dft
*/

#include <stdio.h>
#include <string.h>
#include <complex.h>
#include <math.h>

typedef  unsigned char  ui8_t;

static int  option_verbose = 0,
            option_raw = 0,
            option_dft = 0,
            option_json = 0,
            wavloaded = 0;


typedef struct {
    int frnr;
    int jahr; int monat; int tag;
    int std; int min; int sek;
    float lat; float lon; float alt;
    unsigned chk;
} gpx_t;

static gpx_t gpx;

/* ------------------------------------------------------------------------------------ */


#define BAUD_RATE 2400

static unsigned int sample_rate = 0;
static int bits_sample = 0, channels = 0;
//float samples_per_bit = 0;

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

    //samples_per_bit = sample_rate/(float)BAUD_RATE;
    //fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);

    return 0;
}

#define EOF_INT  0x1000000
static unsigned int sample_count;

static int read_signed_sample(FILE *fp) {  // int = i32_t
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

    if (bits_sample ==  8) return ret-128;
    if (bits_sample == 16) return (short)ret;

    return ret;
}

/* ------------------------------------------------------------------------------------ */


#define LOG2N   6 // 2^7 = 128 = N
#define N      64 // 128  Vielfaches von 22 oder 10 unten
#define WLEN   40 // (2*(48000/BAUDRATE))

#define BITS 10   // (8N1: 0 bbbbbbbb 1)
#define LEN_BITFRAME  (9*BITS)
#define HEADLEN 20

static char header[] =   "00000000010111111111"; // 0x00 0xFF
static char buf[HEADLEN+1] = "x";
static int bufpos = -1;

static int    bitpos;
static ui8_t  bits[LEN_BITFRAME+20] = { 0,  0, 0, 0, 0, 0, 0, 0, 0,  1,
                                        0,  1, 1, 1, 1, 1, 1, 1, 1,  1};
static ui8_t  bytes[LEN_BITFRAME/BITS+2];

static float  x[N];
static float complex  Z[N], w[N], expw[N][N], ew[N*N];

static int    ptr;
static float  Hann[N], buffer[N+1], xn[N];


static void init_dft() {
    int i, k, n;

    for (i = 0; i < N; i++)     Hann[i] = 0;
    for (i = 0; i < WLEN; i++)  Hann[i] = 0.5 * (1 - cos( 2 * M_PI * i / (float)(WLEN-1) ) );
                              //Hann[i+(N-1-WLEN)/2] = 0.5 * (1 - cos( 2 * M_PI * i / (float)(WLEN-1) ) );

    for (k = 0; k < N; k++) {
        w[k] = -I*2*M_PI * k / (float)N;
        for (n = 0; n < N; n++) {
            expw[k][n] = cexp( w[k] * n );
            ew[k*n] = expw[k][n];
        }
    }
}


static float dft_k(int k) {
    int n;
    float complex  Zk;

    Zk = 0;
    for (n = 0; n < N; n++) {
        Zk += xn[n] * ew[k*n];
    }
    return cabs(Zk);
}

static void dft() {
    int k, n;

    for (k = 0; k < N/2; k++) {  // xn reell, brauche nur N/2 unten
        Z[k] = 0;
        for (n = 0; n < N; n++) {
            Z[k] += xn[n] * ew[k*n];
        }
    }
}

static void dft2() {
    int s, l, l2, i, j, k;
    float complex  w1, w2, T;

    for (i = 0; i < N; i++) {
        Z[i] = (float complex)xn[i];
    }

    j = 1;
    for (i = 1; i < N; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = N/2;
        while (k < j) {
            j = j - k;
            k = k/2;
        }
        j = j + k;
    }

    for (s = 0; s < LOG2N; s++) {
        l2 = 1 << s;
        l  = l2 << 1;
        w1 = (float complex)1.0;
        w2 = cexp(-I*M_PI/(float)l2);
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= N; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}

static int max_bin() {
    int k, kmax;
    float max;

    max = 0; kmax = 0;
    for (k = 0; k < N/2-1; k++) {
        if (cabs(Z[k]) > max) {
            max = cabs(Z[k]);
            kmax = k;
        }
    }

    return kmax;
}

static float freq2bin(int f) {
    return  f * N / (float)sample_rate;
}

static int bin2freq(int k) {
    return  sample_rate * k / N;
}

/* ------------------------------------------------------------------------------------ */


static void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

static int compare() {
    int i=0, j = bufpos;

    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADLEN-1-i]) break;
        j--;
        i++;
    }
    return i;
}

static int bits2bytes8N1(ui8_t bits[], ui8_t bytes[], int n) {
    int i, j, byteval=0, d=1;

    for (j = 0; j < n; j++) {
        byteval=0; d=1;
        for (i = 1; i < BITS-1; i++) {  // little endian
        /* for (i = 7; i >= 0; i--) { // big endian */
            if     (bits[BITS*j+i] == 1)   byteval += d;
            else /*(bits[BITS*j+i] == 0)*/ byteval += 0;
            d <<= 1;
        }
        bytes[j] = byteval;
    }
    return 0;
}

static void printGPX() {
    int i;

        printf(" %04d-%02d-%02d", gpx.jahr, gpx.monat, gpx.tag);
        printf(" %02d:%02d:%02d", gpx.std, gpx.min, gpx.sek);
        printf(" ");
        printf(" lat: %.5f", gpx.lat);
        printf(" lon: %.5f", gpx.lon);
        printf(" alt: %.1f", gpx.alt);

        if (option_verbose) {
            printf("  # ");
            for (i = 0; i < 5; i++) printf("%d", (gpx.chk>>i)&1);
        }

        printf("\n");
}

static void printJSON() {
    // UTC or GPS time ?
    printf("{ \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.1f",
           gpx.jahr, gpx.monat, gpx.tag, gpx.std, gpx.min, gpx.sek, gpx.lat, gpx.lon, gpx.alt);
    printf(" }\n");
    //printf("\n");
}

// Chechsum Fletcher16
static unsigned check2(ui8_t *bytes, int len) {
    int sum1, sum2;
    int i;

    sum1 = 0;
    sum2 = 0;
    for (i = 0; i < len; i++) {
        sum1 += bytes[i];
        sum2 += (len-i)*bytes[i];
    }
    sum1 = sum1 & 0xFF;
    sum2 = (-1-sum2) & 0xFF; // = (~sum2) & 0xFF;

    return sum2 | (sum1<<8);
}
/* // equivalent
static unsigned check16(ui8_t *bytes, int len) {
    unsigned sum1, sum2;
    int i;
    sum1 = sum2 = 0;
    for (i = 0; i < len; i++) {
        sum1 = (sum1 + bytes[i]) % 0x100;
        sum2 = (sum2 + sum1) % 0x100;
    }
    sum2 = (~sum2) & 0xFF;  // 1's complement
    return sum2 | (sum1<<8);
}
*/

static float NMEAll2(int ll) {  // NMEA GGA,GLL: ll/1e5=(D)DDMM.mmmm
    int deg = ll / 10000000;
    float min = (ll - deg*10000000)/1e5;
    return deg+min/60.0;
}

static int evalBytes2() {
    int i, val = 0;
    ui8_t id = bytes[2];
    unsigned check;
    static unsigned int cnt_dat = -1, cnt_tim = -1,
                        cnt_lat = -1, cnt_lon = -1, cnt_alt = -1;

    check = ((bytes[7]<<8)|bytes[8]) != check2(bytes+2, 5);

    for (i = 0; i < 4; i++)  val |= bytes[6-i] << (8*i);

    if      (id == 0x14 ) {  // date
        int tag = val / 10000;
        int mon = (val-tag*10000) / 100;
        int jrz = val % 100;
        gpx.tag = tag;
        gpx.monat = mon;
        gpx.jahr = 2000+jrz;
        gpx.chk = (gpx.chk & ~(0x1<<0)) | (check<<0);
        if (check==0) cnt_dat = sample_count;
    }
    else if (id == 0x15 ) {  // time
        int std = val / 10000;
        int min = (val-std*10000) / 100;
        int sek = val % 100;
        gpx.std = std;
        gpx.min = min;
        gpx.sek = sek;
        gpx.chk = (gpx.chk & ~(0x1<<1)) | (check<<1);
        if (check==0) cnt_tim = sample_count;
    }
    else if (id == 0x16 ) {  // lat: wie NMEA mit Faktor 1e5
        gpx.lat = NMEAll2(val);
        gpx.chk = (gpx.chk & ~(0x1<<2)) | (check<<2);
        if (check==0) cnt_lat = sample_count;
    }
    else if (id == 0x17 ) {  // lon: wie NMEA mit Faktor 1e5
        gpx.lon = NMEAll2(val);
        gpx.chk = (gpx.chk & ~(0x1<<3)) | (check<<3);
        if (check==0) cnt_lon = sample_count;
    }
    else if (id == 0x18 ) {  // alt: decimeter
        gpx.alt = val/10.0;
        gpx.chk = (gpx.chk & ~(0x1<<4)) | (check<<4);
        if (check==0) cnt_alt = sample_count;
    }

    if (id == 0x18) {
        printGPX();
        if (option_json && check==0) {
            if ( ((cnt_dat|cnt_tim|cnt_lat|cnt_lon)&0x80000000)==0 &&
                 cnt_alt - cnt_dat < sample_rate &&
                 cnt_alt - cnt_tim < sample_rate &&
                 cnt_alt - cnt_lat < sample_rate &&
                 cnt_alt - cnt_lon < sample_rate )
            {
                printJSON();
            }
        }
    }

    return check;
}

static void printRaw(int n) {
    int j;
    unsigned chkbyt = (bytes[7]<<8) | bytes[8];
    unsigned chksum = check2(bytes+2, 5);
    //if (chksum == chkbyt)
    {
        for (j = 0; j < LEN_BITFRAME; j++) {
            if (j%BITS == 1) printf(" ");
            if (j%BITS == 9) printf(" ");
            printf("%d", bits[j]);
        }
        printf("  :  ");
        printf("%02X%02X ", bytes[0], bytes[1]);
        printf("%02X ", bytes[2]);
        printf("%02X%02X%02X%02X ", bytes[3], bytes[4], bytes[5], bytes[6]);
        printf("%02X%02X", bytes[7], bytes[8]); // chkbyt
        if (option_verbose) {
            printf("  #  %04X", chksum);
            if (chksum == chkbyt) printf(" [OK]"); else printf(" [NO]");
        }
        printf("\n");
    }
}


int main(int argc, char *argv[]) {

    FILE *fp;
    char *fpname;
    int sample;
    int i, j, kmax, k0, k1;
    int bit = 8, bit0 = 8;
    int pos = 0, pos0 = 0;
    int header_found = 0;
    int bitlen; // sample_rate/BAUD_RATE
    int len;
    float k_f0, k_f1, k_df;
    float cb0, cb1;

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
        else if ( (strcmp(*argv, "-d1") == 0) || (strcmp(*argv, "--dft1") == 0) ) {
            option_dft = 1;
        }
        else if ( (strcmp(*argv, "-d2") == 0) || (strcmp(*argv, "--dft2") == 0) ) {
            option_dft = 2;
        }
        else if ( (strcmp(*argv, "--json") == 0) ) {
            option_dft = 0;
            option_verbose = 1;
            option_json = 1;
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
    if (i) {
        fclose(fp);
        return -1;
    }


    bitlen = sample_rate/BAUD_RATE;
    k_f0 = freq2bin(4700);  // bit0: 4800Hz
    k_f1 = freq2bin(2900);  // bit1: 3000Hz
    k_df = fabs(k_f0-k_f1)/2.5;
    k0 = (int)(k_f0+.5);
    k1 = (int)(k_f1+.5);

    init_dft();

    ptr = -1; sample_count = -1;
    while ((sample=read_signed_sample(fp)) < EOF_INT) {

        ptr++;
        sample_count++;
        if (ptr == N) ptr = 0;
        buffer[ptr] = sample / (float)(1<<bits_sample);

        if (sample_count < N) continue;


            for (j = 0; j < N; j++) {
                xn[j] = Hann[j]*buffer[(ptr + j + 1)%N];
            }

            if (option_dft) {
                if (option_dft == 2) dft2();
                else                 dft();
                kmax = max_bin();
                if      (kmax > k_f0-k_df  &&  kmax < k_f0+k_df)  bit = 0;  // kmax = freq2bin(4800): 4800Hz
                else if (kmax > k_f1-k_df  &&  kmax < k_f1+k_df)  bit = 1;  // kmax = freq2bin(3000): 3000Hz
            }
            else {
                cb0 = dft_k(k0);
                cb1 = dft_k(k1);
                if      ( cb0 > cb1 )  bit = 0;  // freq2bin(4800) : 4800Hz
                else                   bit = 1;  // freq2bin(3000) : 3000Hz
            }

            if (bit != bit0) {

                pos0 = pos;
                pos = sample_count;  //sample_count-(N-1)/2

                len = (pos-pos0+bitlen/2)/bitlen; //(pos-pos0)/bitlen + 0.5;
                for (i = 0; i < len; i++) {
                    inc_bufpos();
                    buf[bufpos] = 0x30 + bit0;

                    if (!header_found) {
                        if (compare() >= HEADLEN) {
                            header_found = 1;
                            for (bitpos = 0; bitpos < HEADLEN; bitpos++) bits[bitpos] = header[bitpos] & 0x1;
                        }
                    }
                    else {
                        bits[bitpos] = bit0;
                        bitpos++;
                        if (bitpos >= LEN_BITFRAME) {

                            bits2bytes8N1(bits, bytes, bitpos/BITS);

                            if (option_raw) {
                                printRaw(bitpos/BITS);
                            }
                            else {
                                evalBytes2();
                            }

                            bitpos = 0;
                            header_found = 0;
                        }
                    }
                }
                bit0 = bit;
            }

    }
    printf("\n");

    fclose(fp);

    return 0;
}

