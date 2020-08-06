
/*
 *  compile:
 *      gcc -c iq_base.c
 *  speedup:
 *      gcc -O2 -c iq_base.c
 *   or
 *      gcc -Ofast -c iq_base.c
 *
 *  author: zilog80
 */

/* ------------------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iq_base.h"

/* ------------------------------------------------------------------------------------ */


//static
void raw_dft(dft_t *dft, float complex *Z) {
    int s, l, l2, i, j, k;
    float complex  w1, w2, T;
    float complex  _1 = (float complex)1.0;

    j = 1;
    for (i = 1; i < dft->N; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = dft->N/2;
        while (k < j) {
            j = j - k;
            k = k/2;
        }
        j = j + k;
    }

    for (s = 0; s < dft->LOG2N; s++) {
        l2 = 1 << s;
        l  = l2 << 1;
        w1 = _1;
        w2 = dft->ew[s]; // cexp(-I*M_PI/(float)l2)
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= dft->N; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}

static void cdft(dft_t *dft, float complex *z, float complex *Z) {
    int i;
    for (i = 0; i < dft->N; i++)  Z[i] = z[i];
    raw_dft(dft, Z);
}

static void rdft(dft_t *dft, float *x, float complex *Z) {
    int i;
    for (i = 0; i < dft->N; i++)  Z[i] = (float complex)x[i];
    raw_dft(dft, Z);
}

static void Nidft(dft_t *dft, float complex *Z, float complex *z) {
    int i;
    for (i = 0; i < dft->N; i++)  z[i] = conj(Z[i]);
    raw_dft(dft, z);
    // idft():
    // for (i = 0; i < dft->N; i++)  z[i] = conj(z[i])/(float)dft->N; // hier: z reell
}

static float bin2freq0(dft_t *dft, int k) {
    float fq = dft->sr * k / /*(float)*/dft->N;
    if (fq >= dft->sr/2.0) fq -= dft->sr;
    return fq;
}
//static
float bin2freq(dft_t *dft, int k) {
    float fq = k / (float)dft->N;
    if ( fq >= 0.5) fq -= 1.0;
    return fq*dft->sr;
}
//static
float bin2fq(dft_t *dft, int k) {
    float fq = k / (float)dft->N;
    if ( fq >= 0.5) fq -= 1.0;
    return fq;
}

static int max_bin(dft_t *dft, float complex *Z) {
    int k, kmax;
    double max;

    max = 0; kmax = 0;
    for (k = 0; k < dft->N; k++) {
        if (cabs(Z[k]) > max) {
            max = cabs(Z[k]);
            kmax = k;
        }
    }

    return kmax;
}

static int dft_window(dft_t *dft, int w) {
    int n;

    if (w < 0 || w > 3) return -1;

    for (n = 0; n < dft->N2; n++) {
        switch (w)
        {
            case 0: // (boxcar)
                    dft->win[n] = 1.0;
                    break;
            case 1: // Hann
                    dft->win[n] = 0.5 * ( 1.0 - cos(2*M_PI*n/(float)(dft->N2-1)) );
                    break ;
            case 2: // Hamming
                    dft->win[n] = 25/46.0 + (1.0 - 25/46.0)*cos(2*M_PI*n / (float)(dft->N2-1));
                    break ;
            case 3: // Blackmann
                    dft->win[n] =  7938/18608.0
                                 - 9240/18608.0*cos(2*M_PI*n / (float)(dft->N2-1))
                                 + 1430/18608.0*cos(4*M_PI*n / (float)(dft->N2-1));
                    break ;
        }
    }
    while (n < dft->N) dft->win[n++] = 0.0;

    return 0;
}


//static double ilog102 = 0.434294482/2.0; // log(10)/2
void db_power(dft_t *dft, float complex Z[], float db[]) {  // iq-samples/V [-1..1]
    int i;                                        // dBw = 2*dBv, P=c*U*U
    for (i = 0; i < dft->N; i++) {                // dBw = 2*10*log10(V/V0)
        db[i] = 20.0 * log10(cabs(Z[i])/dft->N2+1e-20);   // 20log10(Z/N)
    }
}

/* ------------------------------------------------------------------------------------ */

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

float read_wav_header(pcm_t *pcm) {
    FILE *fp = pcm->fp;
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;
    int sample_rate = 0, bits_sample = 0, channels = 0;

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


    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;


    pcm->sr  = sample_rate;
    pcm->bps = bits_sample;
    pcm->nch = channels;

    return 0;
}

typedef struct {
    double sumIQx;
    double sumIQy;
    float avgIQx;
    float avgIQy;
    ui32_t cnt;
    ui32_t maxcnt;
    ui32_t maxlim;
} iq_dc_t;
static iq_dc_t IQdc;

int iq_dc_init(pcm_t *pcm) {
    memset(&IQdc, 0, sizeof(IQdc));
    IQdc.maxlim = pcm->sr;
    IQdc.maxcnt = IQdc.maxlim/32; // 32,16,8,4,2,1
    if (pcm->decM > 1) {
        IQdc.maxlim *= pcm->decM;
        IQdc.maxcnt *= pcm->decM;
    }

    return 0;
}

static int f32read_csample(dsp_t *dsp, float complex *z) {

    float x, y;

    if (dsp->bps == 32) { //float32
        float f[2];
        if (fread( f, dsp->bps/8, 2, dsp->fp) != 2) return EOF;
        x = f[0];
        y = f[1];
    }
    else if (dsp->bps == 16) { //int16
        short b[2];
        if (fread( b, dsp->bps/8, 2, dsp->fp) != 2) return EOF;
        x = b[0]/32768.0;
        y = b[1]/32768.0;
    }
    else {  // dsp->bps == 8   //uint8
        ui8_t u[2];
        if (fread( u, dsp->bps/8, 2, dsp->fp) != 2) return EOF;
        x = (u[0]-128)/128.0;
        y = (u[1]-128)/128.0;
    }

    *z = (x - IQdc.avgIQx) + I*(y - IQdc.avgIQy);

    IQdc.sumIQx += x;
    IQdc.sumIQy += y;
    IQdc.cnt += 1;
    if (IQdc.cnt == IQdc.maxcnt) {
        IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
        IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
        IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
        if (IQdc.maxcnt < IQdc.maxlim) IQdc.maxcnt *= 2;
    }

    return 0;
}


volatile int bufeof = 0;      // threads exit
volatile int rbf1;
static volatile int rbf;


#ifdef CLK
#include <time.h>
static struct timespec t_1;
static unsigned int in_smp;
static int t_init = 0;
static double t_acc = 0;
#endif

static int f32_cblk(dsp_t *dsp) {

    int n;
    int BL = dsp->decM * blk_sz;
    int len = BL;
    float x, y;
    ui8_t s[4*2*BL]; //uin8,int16,flot32
    ui8_t *u = (ui8_t*)s;
    short *b = (short*)s;
    float *f = (float*)s;

    #ifdef CLK
    if ( t_init == 0 ) {
        t_init = 1;
        clock_gettime(CLOCK_REALTIME, &t_1);
    }
    #endif


    len = fread( s, dsp->bps/8, 2*BL, dsp->fp) / 2;

    //for (n = 0; n < len; n++) dsp->thd->blk[n] = (u[2*n]-128)/128.0 + I*(u[2*n+1]-128)/128.0;
    // u8: 0..255, 128 -> 0V
    for (n = 0; n < len; n++) {
        if (dsp->bps == 8) { //uint8
            x = (u[2*n  ]-128)/128.0;
            y = (u[2*n+1]-128)/128.0;
        }
        else if (dsp->bps == 16) { //int16
            x = b[2*n  ]/32768.0;
            y = b[2*n+1]/32768.0;
        }
        else { // dsp->bps == 32   //float32
            x = f[2*n];
            y = f[2*n+1];
        }

        dsp->thd->blk[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);

        IQdc.sumIQx += x;
        IQdc.sumIQy += y;
        IQdc.cnt += 1;
        if (IQdc.cnt == IQdc.maxcnt) {
            IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
            IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
            IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            if (IQdc.maxcnt < IQdc.maxlim) IQdc.maxcnt *= 2;
        }
    }
    if (len < BL) bufeof = 1;

    #ifdef CLK
    in_smp += len;
    if (in_smp >= dsp->sr_base) {
        double s_d = in_smp / (double)dsp->sr_base;
        double t_d = 0;
        struct timespec t_2;
        clock_gettime(CLOCK_REALTIME, &t_2);
        t_d  = (t_2.tv_sec  - t_1.tv_sec);
        t_d += (t_2.tv_nsec - t_1.tv_nsec)/1e9;
        if (t_init > 1 && t_d > 0.9) t_acc += t_d - s_d;
        else t_init = 2;

        if (dsp->opt_dbg) {
            fprintf(stderr, "insmp: %d    dt: %.3f     s_d: %.3f    t_acc: %.3f\n", in_smp, t_d, s_d, t_acc);
        }

        t_1 = t_2;
        in_smp = 0;
    }
    #endif

    return len;
}

static int f32read_cblock(dsp_t *dsp) { // blk_cond

    int n;
    int len = dsp->decM;

    if (bufeof) return 0;
    //if (dsp->thd->used == 0) { }

    pthread_mutex_lock( dsp->thd->mutex );

    if (rbf == 0)
    {
        len = f32_cblk(dsp);

        rbf = rbf1; // set all bits
        pthread_cond_broadcast( dsp->thd->cond );
    }

    while ((rbf & dsp->thd->tn_bit) == 0) pthread_cond_wait( dsp->thd->cond, dsp->thd->mutex );

    for (n = 0; n < dsp->decM; n++) dsp->decMbuf[n] = dsp->thd->blk[dsp->decM*dsp->blk_cnt + n];

    dsp->blk_cnt += 1;
    if (dsp->blk_cnt == blk_sz) {
        rbf &= ~(dsp->thd->tn_bit); // clear bit(tn)
        dsp->blk_cnt = 0;
    }

    pthread_mutex_unlock( dsp->thd->mutex );

    return len;
}

int reset_blockread(dsp_t *dsp) {

    int len = 0;

    pthread_mutex_lock( dsp->thd->mutex );

    rbf1 &= ~(dsp->thd->tn_bit);

    if ( (rbf & dsp->thd->tn_bit) == dsp->thd->tn_bit )
    {
        len = f32_cblk(dsp);

        rbf = rbf1; // set all bits
        pthread_cond_broadcast( dsp->thd->cond );
    }
    pthread_mutex_unlock( dsp->thd->mutex );

    return len;
}

// decimate lowpass
static float *ws_dec;

static double sinc(double x) {
    double y;
    if (x == 0) y = 1;
    else y = sin(M_PI*x)/(M_PI*x);
    return y;
}

static int lowpass_init(float f, int taps, float **pws) {
    double *h, *w;
    double norm = 0;
    int n;
    float *ws = NULL;

    if (taps % 2 == 0) taps++; // odd/symmetric

    if ( taps < 1 ) taps = 1;

    h = (double*)calloc( taps+1, sizeof(double)); if (h == NULL) return -1;
    w = (double*)calloc( taps+1, sizeof(double)); if (w == NULL) return -1;
    ws = (float*)calloc( 2*taps+1, sizeof(float)); if (ws == NULL) return -1;

    for (n = 0; n < taps; n++) {
        w[n] = 7938/18608.0 - 9240/18608.0*cos(2*M_PI*n/(taps-1)) + 1430/18608.0*cos(4*M_PI*n/(taps-1)); // Blackmann
        h[n] = 2*f*sinc(2*f*(n-(taps-1)/2));
        ws[n] = w[n]*h[n];
        norm += ws[n]; // 1-norm
    }
    for (n = 0; n < taps; n++) {
        ws[n] /= norm; // 1-norm
    }

    for (n = 0; n < taps; n++) ws[taps+n] = ws[n]; // duplicate/unwrap

    *pws = ws;

    free(h); h = NULL;
    free(w); w = NULL;

    return taps;
}

int decimate_init(float f, int taps) {
    return lowpass_init(f, taps, &ws_dec);
}

int decimate_free() {
    if (ws_dec) { free(ws_dec); ws_dec = NULL; }
    return 0;
}

static float complex lowpass0(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    double complex w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[(sample+n+1)%taps]*ws[taps-1-n];
    }
    return (float complex)w;
}
static float complex lowpass(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    ui32_t s = sample % taps;
    double complex w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[taps+s-n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return (float complex)w;
// symmetry: ws[n] == ws[taps-1-n]
}

/* -------------------------------------------------------------------------- */

int read_ifblock(dsp_t *dsp, float complex *z) {

    ui32_t s_reset = dsp->dectaps*dsp->lut_len;
    int j;

    if ( f32read_cblock(dsp) < dsp->decM ) return EOF;
    //if ( f32read_cblock(dsp) < dsp->decM * blk_sz) return EOF;
    for (j = 0; j < dsp->decM; j++) {
        dsp->decXbuffer[dsp->sample_dec % dsp->dectaps] = dsp->decMbuf[j] * dsp->ex[dsp->sample_dec % dsp->lut_len];
        dsp->sample_dec += 1;
        if (dsp->sample_dec == s_reset) dsp->sample_dec = 0;
    }

    *z = lowpass(dsp->decXbuffer, dsp->sample_dec, dsp->dectaps, ws_dec);

    return 0;
}

int read_fftblock(dsp_t *dsp) {

    if ( f32read_cblock(dsp) < dsp->decM ) return EOF;

    return 0;
}

/* -------------------------------------------------------------------------- */

#define IF_TRANSITION_BW (4e3)  // 4kHz transition width
#define FM_TRANSITION_BW (2e3)  // 2kHz transition width



static double norm2_vect(float *vect, int n) {
    int i;
    double x, y = 0.0;
    for (i = 0; i < n; i++) {
        x = vect[i];
        y += x*x;
    }
    return y;
}

#define HZBIN 100

int init_buffers(dsp_t *dsp) {

    float t;
    int n, k;


    if (dsp->thd->scan == 0)
    {
        //
        // pcm_dec_init()
        //

        // lookup table, exp-rotation
        int W = 2*8; // 16 Hz window
        int d = 1; // 1..W , groesster Teiler d <= W von sr_base
        int freq = (int)( dsp->thd->xlt_fq * (double)dsp->sr_base + 0.5);
        int freq0 = freq; // init
        double f0 = freq0 / (double)dsp->sr_base; // init

        for (d = W; d > 0; d--) { // groesster Teiler d <= W von sr
            if (dsp->sr_base % d == 0) break;
        }
        if (d == 0) d = 1; // d >= 1 ?

        for (k = 0; k < W/2; k++) {
            if ((freq+k) % d == 0) {
                freq0 = freq + k;
                break;
            }
            if ((freq-k) % d == 0) {
                freq0 = freq - k;
                break;
            }
        }

        dsp->lut_len = dsp->sr_base / d;
        f0 = freq0 / (double)dsp->sr_base;

        dsp->ex = calloc(dsp->lut_len+1, sizeof(float complex));
        if (dsp->ex == NULL) return -1;
        for (n = 0; n < dsp->lut_len; n++) {
            t = f0*(double)n;
            dsp->ex[n] = cexp(t*2*M_PI*I);
        }


        dsp->decXbuffer = calloc( dsp->dectaps+1, sizeof(float complex));
        if (dsp->decXbuffer == NULL) return -1;

    }
    else {
        dsp->decXbuffer = NULL;
        dsp->ex         = NULL;
    }


    dsp->decMbuf = calloc( dsp->decM+1, sizeof(float complex));
    if (dsp->decMbuf == NULL) return -1;


    dsp->DFT.sr = dsp->sr_base;

    int mn = 0; // 0: N = M

/*
    dsp->DFT.LOG2N = 14;
    dsp->DFT.N2 = 1 << dsp->DFT.LOG2N;
    if (dsp->DFT.N2 > dsp->DFT.sr/2) {
        dsp->DFT.LOG2N = 0;
        while ( (1 << (dsp->DFT.LOG2N+1)) < dsp->DFT.sr/2 ) dsp->DFT.LOG2N++;
        dsp->DFT.N2 = 1 << dsp->DFT.LOG2N;
    }
*/
    dsp->DFT.LOG2N = log(dsp->DFT.sr/HZBIN)/log(2)+0.1;
    if (dsp->DFT.LOG2N < 10) dsp->DFT.LOG2N = 10;
    dsp->DFT.N2 = 1 << dsp->DFT.LOG2N;
    dsp->DFT.N = dsp->DFT.N2 << mn;
    dsp->DFT.LOG2N += mn;

if (dsp->thd->scan) {
//fprintf(stderr, "HZBIN: %d , N: %d , Hz_per_bin: %.1f\n", HZBIN, dsp->DFT.N, bin2freq(&(dsp->DFT), 1));
}

    dsp->DFT.X  = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.X  == NULL) return -1;
    dsp->DFT.Z  = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.Z  == NULL) return -1;

    dsp->DFT.ew = calloc(dsp->DFT.LOG2N+1, sizeof(float complex));  if (dsp->DFT.ew == NULL) return -1;

    // FFT window
    // a) N2 = N
    // b) N2 < N (interpolation)
    dsp->DFT.win = calloc(dsp->DFT.N+1, sizeof(float complex));  if (dsp->DFT.win == NULL) return -1; // float real
    dsp->DFT.N2 = dsp->DFT.N;
    //dsp->DFT.N2 = dsp->DFT.N/2 - 1; // N=2^log2N
    dft_window(&dsp->DFT, 1);

    for (n = 0; n < dsp->DFT.LOG2N; n++) {
        k = 1 << n;
        dsp->DFT.ew[n] = cexp(-I*M_PI/(float)k);
    }

    return 0;
}

int free_buffers(dsp_t *dsp) {


    if (dsp->DFT.ew) { free(dsp->DFT.ew); dsp->DFT.ew = NULL; }
    if (dsp->DFT.X)  { free(dsp->DFT.X);  dsp->DFT.X  = NULL; }
    if (dsp->DFT.Z)  { free(dsp->DFT.Z);  dsp->DFT.Z  = NULL; }

    if (dsp->DFT.win) { free(dsp->DFT.win); dsp->DFT.win = NULL; }

    if (dsp->decMbuf)    { free(dsp->decMbuf);    dsp->decMbuf    = NULL; }


    if (dsp->decXbuffer) { free(dsp->decXbuffer); dsp->decXbuffer = NULL; }
    if (dsp->ex)         { free(dsp->ex);         dsp->ex         = NULL; }


    return 0;
}


