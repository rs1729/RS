
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef short i16_t;
typedef int   i32_t;


static int option_verbose = 0,  // ausfuehrliche Anzeige
           option_inv = 0,      // invertiert Signal
           option_dc = 0,
           wavloaded = 0;
static int wav_channel = 0;     // audio channel: left


//int  dfm_bps = 2500;
static char dfm_header[] = "01100101011001101010010110101010";

//int  vai_bps = 4800;
static char rs41_header[] = "00001000011011010101001110001000"
                            "01000100011010010100100000011111";
static char rs92_header[] = //"10100110011001101001"
                            //"10100110011001101001"
                            //"10100110011001101001"
                            "10100110011001101001"
                            "1010011001100110100110101010100110101001";

//int  lms_bps = 4800;
static char lms6_header[] = "0101011000001000""0001110010010111"
                            "0001101010100111""0011110100111110";

//int  m10_bps = 9600;
static char m10_header[] = "10011001100110010100110010011001";
// frame byte[0..1]: byte[0]=framelen-1, byte[1]=type(8F=M2K2,9F=M10,AF=M10+)
// M2K2   : 64 8F : 0110010010001111
// M10    : 64 9F : 0110010010011111 (framelen 0x64+1)
// M10-aux: 76 9F : 0111011010011111 (framelen 0x76+1)
// M10+   : 64 AF : 0110010010101111 (w/ gtop-GPS)

//int  imet1ab_bps = 9600;
static char imet1ab_header[] = "11110000111100001111000011110000"
                     "11110000""10101100110010101100101010101100"
                     "11110000""10101100110010101100101010101100";

typedef struct {
    int bps;  // header: here bps means baudrate ...
    int hLen;
    int N;
    char *header;
    float BT;
    float spb;
    float thres;
    float complex *Fm;
    char *type;
} rsheader_t;

#define Nrs 6
static rsheader_t rs_hdr[Nrs] = {
    { 2500, 0, 0, dfm_header,     0.5, 0.0, 0.60, NULL, "DFM"},
    { 4800, 0, 0, rs41_header,    0.5, 0.0, 0.70, NULL, "RS41"},
    { 4800, 0, 0, rs92_header,    0.5, 0.0, 0.70, NULL, "RS92"},
    { 4800, 0, 0, lms6_header,    1.0, 0.0, 0.70, NULL, "LMS6"},
    { 9600, 0, 0, m10_header,     1.0, 0.0, 0.76, NULL, "M10"},
    { 9600, 0, 0, imet1ab_header, 1.0, 0.0, 0.70, NULL, "IMET1AB"}
};

/*
// m10-false-positive:
// m10-preamble similar to rs41-preamble, parts of rs92/imet1ab; diffs:
// - iq: - modulation-index rs41 < rs92 < m10,
//       - power level / frame < 1s, noise
// - fm: - frame duration <-> noise (variance/standard deviation)
//       - pulse-shaping
//           m10: 00110011 at 9600 bps
//           rs41: 0 1 0 1 at 4800 bps
// - m10 top-carrier, fm-mean/average
// - m10-header ..110(1)0110011()011.. bit shuffle
// - m10 frame byte[1]=type(M2K2,M10,M10+)
*/


static unsigned int sample_in, sample_out, delay;

static int M; // N

static float *bufs  = NULL;

static char *rawbits = NULL;

static int Nvar = 0; // < M
static double xsum = 0;
static float *xs = NULL;
/*
static double xsum=0, qsum=0;
static float *xs = NULL,
             *qs = NULL;
*/

static float dc_ofs = 0.0;
static float dc = 0.0;

/* ------------------------------------------------------------------------------------ */


static int LOG2N, N_DFT;

static float complex  *ew;

static float complex  *X, *Z, *cx;
static float *xn;

static void dft_raw(float complex *Z) {
    int s, l, l2, i, j, k;
    float complex  w1, w2, T;

    j = 1;
    for (i = 1; i < N_DFT; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = N_DFT/2;
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
        w2 = ew[s]; // cexp(-I*M_PI/(float)l2)
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= N_DFT; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}

static void dft(float *x, float complex *Z) {
    int i;
    for (i = 0; i < N_DFT; i++)  Z[i] = (float complex)x[i];
    dft_raw(Z);
}

static void Nidft(float complex *Z, float complex *z) {
    int i;
    for (i = 0; i < N_DFT; i++)  z[i] = conj(Z[i]);
    dft_raw(z);
    // idft():
    // for (i = 0; i < N_DFT; i++)  z[i] = conj(z[i])/(float)N_DFT; // hier: z reell
}

/* ------------------------------------------------------------------------------------ */
/*
static float get_bufvar(int ofs) {
    float mu  = xs[(sample_out+M + ofs) % M]/Nvar;
    float var = qs[(sample_out+M + ofs) % M]/Nvar - mu*mu;
    return var;
}
*/
static float get_bufmu(int ofs) {
    float mu  = xs[(sample_out+M + ofs) % M]/Nvar;
    return mu;
}


static int getCorrDFT(int abs, int K, unsigned int pos, float *maxv, unsigned int *maxvpos, rsheader_t rshd) {
    int i;
    int mp = -1;
    float mx = 0.0;
    double xnorm = 1;
    unsigned int mpos = 0;

    dc = 0.0;

    if (rshd.N + K > N_DFT/2 - 2) return -1;
    if (sample_in < delay+rshd.N+K) return -2;

    if (pos == 0) pos = sample_out;


    for (i = 0; i < rshd.N+K; i++) xn[i] = bufs[(pos+M -(rshd.N+K-1) + i) % M];
    while (i < N_DFT) xn[i++] = 0.0;

    dft(xn, X);

    dc = get_bufmu(pos-sample_out); //oder: dc = creal(X[0])/N_DFT;

    for (i = 0; i < N_DFT; i++) Z[i] = X[i]*rshd.Fm[i];

    Nidft(Z, cx);


    if (abs) {
        for (i = rshd.N; i < rshd.N+K; i++) {
            if (fabs(creal(cx[i])) > fabs(mx)) {  // imag(cx)=0
                mx = creal(cx[i]);
                mp = i;
            }
        }
    }
    else {
        for (i = rshd.N; i < rshd.N+K; i++) {
            if (creal(cx[i]) > mx) {  // imag(cx)=0
                mx = creal(cx[i]);
                mp = i;
            }
        }
    }
    if (mp == rshd.N || mp == rshd.N+K-1) return -4; // Randwert

    mpos = pos - ( rshd.N+K-1 - mp );

    //xnorm = sqrt(qs[(mpos + 2*M) % M]);
    xnorm = 0.0;
    for (i = 0; i < rshd.N; i++) xnorm += bufs[(mpos-i + M) % M]*bufs[(mpos-i + M) % M];
    xnorm = sqrt(xnorm);

    mx /= xnorm*N_DFT;

    *maxv = mx;
    *maxvpos = mpos;


    return mp;
}

/* ------------------------------------------------------------------------------------ */

static int sample_rate = 0, bits_sample = 0, channels = 0;
static int wav_ch = 0;  // 0: links bzw. mono; 1: rechts

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static int read_wav_header(FILE *fp, int wav_channel) {
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

    if (wav_channel >= 0  &&  wav_channel < channels) wav_ch = wav_channel;
    else wav_ch = 0;
    fprintf(stderr, "channel-In : %d\n", wav_ch+1);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    return 0;
}

static int f32read_sample(FILE *fp, float *s) {
    int i;
    short b = 0;

    for (i = 0; i < channels; i++) {

        if (fread( &b, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == wav_ch) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (bits_sample ==  8) { b -= 128; }
            *s = b/128.0;
            if (bits_sample == 16) { *s /= 256.0; }
        }
    }

    return 0;
}


static int f32buf_sample(FILE *fp, int inv, int cm) {
    float s = 0.0;
    float xneu, xalt;


    if (f32read_sample(fp, &s) == EOF) return EOF;

    if (inv) s = -s;
    bufs[sample_in % M] = s  - dc_ofs;

    xneu = bufs[(sample_in  ) % M];
    xalt = bufs[(sample_in+M - Nvar) % M];
    xsum +=  xneu - xalt;                 // + xneu - xalt
    xs[sample_in % M] = xsum;
/*
    qsum += (xneu - xalt)*(xneu + xalt);  // + xneu*xneu - xalt*xalt
    qs[sample_in % M] = qsum;
*/

    if (0 && cm) {
        // direct correlation
    }


    sample_out = sample_in - delay;

    sample_in += 1;

    return 0;
}

static int read_bufbit(int symlen, char *bits, unsigned int mvp, int reset, float spb) {
// symlen==2: manchester2 0->10,1->01->1: 2.bit

    static unsigned int rcount;
    static float rbitgrenze;

    double sum = 0.0;

    if (reset) {
        rcount = 0;
        rbitgrenze = 0;
    }


    rbitgrenze += spb;
    do {
        sum += bufs[(rcount + mvp + M) % M];
        rcount++;
    } while (rcount < rbitgrenze);  // n < spb

    if (symlen == 2) {
        rbitgrenze += spb;
        do {
            sum -= bufs[(rcount + mvp + M) % M];
            rcount++;
        } while (rcount < rbitgrenze);  // n < spb
    }


    if (symlen != 2) {
        if (sum >= 0) *bits = '1';
        else          *bits = '0';
    }
    else {
        if (sum >= 0) strncpy(bits, "10", 2);
        else          strncpy(bits, "01", 2);
    }

    return 0;
}

static int headcmp(int symlen, char *hdr, int len, unsigned int mvp, int inv, int option_dc, float spb) {
    int errs = 0;
    int pos;
    int step = 1;
    char sign = 0;

    if (symlen != 1) step = 2;
    if (inv) sign=1;

    for (pos = 0; pos < len; pos += step) {
        read_bufbit(symlen, rawbits+pos, mvp+1-(int)(len*spb), pos==0, spb);
    }
    rawbits[pos] = '\0';

    while (len > 0) {
        if ((rawbits[len-1]^sign) != hdr[len-1]) errs += 1;
        len--;
    }

    if (option_dc && errs < 3) {
        dc_ofs += dc;
    }

    return errs;
}

/* -------------------------------------------------------------------------- */


#define SQRT2 1.4142135624   // sqrt(2)
// sigma = sqrt(log(2)) / (2*PI*BT):
//#define SIGMA 0.2650103635   // BT=0.5: 0.2650103635 , BT=0.3: 0.4416839392

// Gaussian FM-pulse
static double Q(double x) {
    return 0.5 - 0.5*erf(x/SQRT2);
}
static double pulse(double t, double sigma) {
    return Q((t-0.5)/sigma) - Q((t+0.5)/sigma);
}


static double norm2_match(float *match, int n) {
    int i;
    double x, y = 0.0;
    for (i = 0; i < n; i++) {
        x = match[i];
        y += x*x;
    }
    return y;
}

static int init_buffers() {

    int i, j, pos;
    double t;
    double b0, b1, b2, b;
    float normMatch;

    int K, NN;
    int n, k;
    float *match = NULL;
    float *m = NULL;

    double BT = 0.5;
    double sigma = sqrt(log(2)) / (2*M_PI*BT);

    char *bits = NULL;
    float spb = 0.0;

    int hLen = 0;

    for (j = 0; j < Nrs; j++) {
        rs_hdr[j].spb = sample_rate/(float)rs_hdr[j].bps;
        rs_hdr[j].hLen = strlen(rs_hdr[j].header);
        rs_hdr[j].N = rs_hdr[j].hLen * rs_hdr[j].spb + 0.5;
        if (rs_hdr[j].hLen > hLen) hLen = rs_hdr[j].hLen;
    }

    NN = hLen * sample_rate/2500.0 + 0.5; // max(hLen*spb)

    M = 3*NN;
    //if (samples_per_bit < 6) M = 6*N;

    delay = NN/16;
    sample_in = 0;

    K = M-NN - delay; // N+K < M

    LOG2N = 2 + (int)(log(NN+K)/log(2));
    N_DFT = 1 << LOG2N;

    while (NN + K > N_DFT/2 - 2) {
        LOG2N  += 1;
        N_DFT <<= 1;
    }

    Nvar = NN; // wenn Nvar fuer xnorm, dann Nvar=rshd.N

    rawbits = (char *)calloc( hLen+1, sizeof(char)); if (rawbits == NULL) return -100;
    bufs  = (float *)calloc( M+1, sizeof(float)); if (bufs  == NULL) return -100;
    xs = (float *)calloc( M+1, sizeof(float)); if (xs == NULL) return -100;
/*
    qs = (float *)calloc( M+1, sizeof(float)); if (qs == NULL) return -100;
*/

    xn = calloc(N_DFT+1, sizeof(float));  if (xn == NULL) return -1;

    ew = calloc(LOG2N+1, sizeof(complex float));  if (ew == NULL) return -1;
    X  = calloc(N_DFT+1, sizeof(complex float));  if (X  == NULL) return -1;
    Z  = calloc(N_DFT+1, sizeof(complex float));  if (Z  == NULL) return -1;
    cx = calloc(N_DFT+1, sizeof(complex float));  if (cx == NULL) return -1;

    for (n = 0; n < LOG2N; n++) {
        k = 1 << n;
        ew[n] = cexp(-I*M_PI/(float)k);
    }

    match = (float *)calloc( NN+1, sizeof(float)); if (match == NULL) return -1;
    m = (float *)calloc(N_DFT+1, sizeof(float));  if (m  == NULL) return -1;


    for (j = 0; j < Nrs; j++)
    {

        rs_hdr[j].Fm = (float complex *)calloc(N_DFT+1, sizeof(complex float));  if (rs_hdr[j].Fm == NULL) return -1;
        bits = rs_hdr[j].header;
        spb = rs_hdr[j].spb;
        sigma = sqrt(log(2)) / (2*M_PI*rs_hdr[j].BT);

        for (i = 0; i < rs_hdr[j].N; i++) {

            pos = i/spb;
            t = (i - pos*spb)/spb - 0.5;

            b1 = ((bits[pos] & 0x1) - 0.5)*2.0;
            b = b1*pulse(t, sigma);

            if (pos > 0) {
                b0 = ((bits[pos-1] & 0x1) - 0.5)*2.0;
                b += b0*pulse(t+1, sigma);
            }

            if (pos < hLen) {
                b2 = ((bits[pos+1] & 0x1) - 0.5)*2.0;
                b += b2*pulse(t-1, sigma);
            }

            match[i] = b;
        }

        normMatch = sqrt(norm2_match(match, rs_hdr[j].N));
        for (i = 0; i < rs_hdr[j].N; i++) {
            match[i] /= normMatch;
        }

        for (i = 0; i < rs_hdr[j].N; i++) m[rs_hdr[j].N-1 - i] = match[i];
        while (i < N_DFT) m[i++] = 0.0;
        dft(m, rs_hdr[j].Fm);

    }


    free(match); match = NULL;
    free(m); m = NULL;

    return K;
}

static int free_buffers() {
    int j;

    if (bufs)  { free(bufs);  bufs  = NULL; }
    if (xs)  { free(xs);  xs  = NULL; }
/*
    if (qs)  { free(qs);  qs  = NULL; }
*/
    if (rawbits) { free(rawbits); rawbits = NULL; }

    if (xn) { free(xn); xn = NULL; }
    if (ew) { free(ew); ew = NULL; }
    if (X)  { free(X);  X  = NULL; }
    if (Z)  { free(Z);  Z  = NULL; }
    if (cx) { free(cx); cx = NULL; }

    for (j = 0; j < Nrs; j++) {
        if (rs_hdr[j].Fm) { free(rs_hdr[j].Fm); rs_hdr[j].Fm = NULL; }
    }

    return 0;
}

/* ------------------------------------------------------------------------------------ */


int main(int argc, char **argv) {

    FILE *fp = NULL;
    char *fpname = NULL;

    int j;
    int k, K;
    float mv[Nrs];
    unsigned int mv_pos[Nrs], mv0_pos[Nrs];
    int mp[Nrs];

    int header_found = 0;
    int herrs;
    float thres = 0.76;


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
            //fprintf(stderr, "       -v, --verbose\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;  // nicht noetig
        }
        else if ( (strcmp(*argv, "--dc") == 0) ) {
            option_dc = 1;
        }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { wav_channel = 1; }  // right channel (default: 0=left)
        else if ( (strcmp(*argv, "--ths") == 0) ) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
                for (j = 0; j < Nrs; j++) rs_hdr[j].thres = thres;
            }
            else return -1;
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


    j = read_wav_header(fp, wav_channel);
    if ( j < 0 ) {
        fclose(fp);
        fprintf(stderr, "error: wav header\n");
        return -1;
    }


    K = init_buffers();
    if ( K < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        return -1;
    };

    for (j = 0; j < Nrs; j++) {
        mv[j] = -1;
        mv_pos[j] = 0;
        mp[j] = 0;
    }

    k = 0;

    while ( f32buf_sample(fp, option_inv, 1) != EOF ) {

        k += 1;

        if (k >= K-4) {
            for (j = 0; j < Nrs; j++) {
                mv0_pos[j] = mv_pos[j];
                mp[j] = getCorrDFT(-1, K, 0, mv+j, mv_pos+j, rs_hdr[j]);
            }
            k = 0;
        }
        else {
            for (j = 0; j < Nrs; j++) mv[j] = 0.0;
            continue;
        }

        header_found = 0;
        for (j = 0; j < Nrs; j++)
        {
            if (mp[j] > 0 && (mv[j] > rs_hdr[j].thres || mv[j] < -rs_hdr[j].thres)) {
                if (mv_pos[j] > mv0_pos[j]) {

                    herrs = headcmp(1, rs_hdr[j].header, rs_hdr[j].hLen, mv_pos[j], mv[j]<0, option_dc, rs_hdr[j].spb);
                    if (herrs < 2) {  // max 1 bitfehler in header
                        fprintf(stdout, "sample: %d\n", mv_pos[j]);
                        fprintf(stdout, "%s: %.4f  (%d)\n", rs_hdr[j].type, mv[j], herrs);
                        header_found = 1;
                    }
                }
            }
        }
        if (header_found) break;
        header_found = 0;

    }


    free_buffers();

    fclose(fp);

    return 0;
}

