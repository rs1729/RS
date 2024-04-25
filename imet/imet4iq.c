
/*
 *  iMet-4 / iMet-1-RS
 *  Bell202 8N1
 *
    gcc imet4iq.c -lm -o imet4iq
    ./imet4iq --iq <fq> imet4_iq.wav
    ./imet4iq --imet1 --iq <fq> imet1_iq.wav
    ./imet4iq fm_audio.wav
    # additional options:
    #   --lpFM   FM lowpass filter
    #   --decFM  FM decimate, reduce FM samples if imet1-IQ
    #   --dc     frequency correction
    #   --json   JSON output
    #   -r       output raw bytes
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif

// optional JSON "version"
//  (a) set global
//      gcc -DVERSION_JSN [-I<inc_dir>] ...
#ifdef VERSION_JSN
  #include "version_jsn.h"
#endif
// or
//  (b) set local compiler option, e.g.
//      gcc -DVER_JSN_STR=\"0.0.2\" ...


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef char  i8_t;
typedef short i16_t;
typedef int   i32_t;

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif
#define _2PI  (6.2831853071795864769252867665590)

#define LP_IQ    1
#define LP_FM    2
#define LP_IQFM  4

#define FM_DEC  2
#define FM_GAIN (0.8)



int option_verbose = 0,  // ausfuehrliche Anzeige
    option_raw = 0,      // rohe Frames
    option_rawbits = 0,
    option_b = 1,
    option_json = 0;

int rawhex = 0;  // raw hex input

/* ------------------------------------------------------------------------------------ */

typedef struct {
    int sr;       // sample_rate
    int bps;      // bits_sample  bits/sample
    int nch;      // channels
    int sel_ch;   // select wav channel
} pcm_t;


int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static
int read_wav_header(pcm_t *pcm, FILE *fp) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;
    int sample_rate = 0, bits_sample = 0, channels = 0;

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

    if (pcm->sel_ch < 0  ||  pcm->sel_ch >= channels) pcm->sel_ch = 0; // default channel: 0
    //fprintf(stderr, "channel-In : %d\n", pcm->sel_ch+1); // nur wenn nicht IQ

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    if (sample_rate == 900001) sample_rate -= 1;

    pcm->sr  = sample_rate;
    pcm->bps = bits_sample;
    pcm->nch = channels;

    return 0;
}


typedef struct {
    FILE *fp;
    //
    int sr;       // sample_rate
    int bps;      // bits/sample
    int nch;      // channels
    int ch;       // select channel
    //
    float sps;    // samples per symbol
    float br;     // baud rate
    //
    ui32_t sample_in;
    ui32_t sample_fm;
    ui32_t sc;
    int M;
    float *bufs;
    float mv;
    ui32_t mv_pos;
    ui32_t pre_pos;
    //

    // IQ-data
    int opt_iq;
    int opt_iqdc;
    float complex iqbuf[2]; // float complex *rot_iqbuf;

    // dc offset
    int opt_dc;
    int locked;
    double dc;
    double Df;
    double dDf;
    float xsum;

    // decimate
    int opt_nolut; // default: LUT
    int opt_IFmin;
    int decM;
    ui32_t sr_base;
    ui32_t dectaps;
    ui32_t sample_decX;
    ui32_t lut_len;
    ui32_t sample_decM;
    float complex *decXbuffer;
    float complex *decMbuf;
    float complex *ex; // exp_lut
    double xlt_fq;

    // IF: lowpass
    int opt_lp;
    int lpIQ_bw;
    float lpIQ_fbw;
    int lpIQtaps; // ui32_t
    float *ws_lpIQ0;
    float *ws_lpIQ1;
    float *ws_lpIQ;
    float complex *lpIQ_buf;

    // FM: lowpass
    int lpFM_bw;
    int lpFMtaps; // ui32_t
    float *ws_lpFM;
    float *lpFM_buf;

    int opt_fmdec;
    int decFM;
    int sr_fm;

    int opt_imet1;

} dsp_t;


static int f32read_sample(dsp_t *dsp, float *s) {
    int i;
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;

    for (i = 0; i < dsp->nch; i++) {

        if (fread( &word, dsp->bps/8, 1, dsp->fp) != 1) return EOF;

        if (i == dsp->ch) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (dsp->bps == 32) {
                *s = *f;
            }
            else {
                if (dsp->bps ==  8) { *b -= 128; }
                *s = *b/128.0;
                if (dsp->bps == 16) { *s /= 256.0; }
            }
        }
    }

    return 0;
}

typedef struct {
    double sumIQx;
    double sumIQy;
    float avgIQx;
    float avgIQy;
    float complex avgIQ;
    ui32_t cnt;
    ui32_t maxcnt;
    ui32_t maxlim;
} iq_dc_t;
static iq_dc_t IQdc;

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

    *z = x + I*y;

    // IQ-dc removal optional
    if (dsp->opt_iqdc) {
        *z -= IQdc.avgIQ;

        IQdc.sumIQx += x;
        IQdc.sumIQy += y;
        IQdc.cnt += 1;
        if (IQdc.cnt == IQdc.maxcnt) {
            IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
            IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
            IQdc.avgIQ  = IQdc.avgIQx + I*IQdc.avgIQy;
            IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            if (IQdc.maxcnt < IQdc.maxlim) IQdc.maxcnt *= 2;
        }
    }

    return 0;
}

static int f32read_cblock(dsp_t *dsp) {

    int n;
    int len;
    float x, y;
    ui8_t s[4*2*dsp->decM]; //uin8,int16,float32
    ui8_t *u = (ui8_t*)s;
    short *b = (short*)s;
    float *f = (float*)s;


    len = fread( s, dsp->bps/8, 2*dsp->decM, dsp->fp) / 2;

    //for (n = 0; n < len; n++) dsp->decMbuf[n] = (u[2*n]-128)/128.0 + I*(u[2*n+1]-128)/128.0;
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

        // baseband: IQ-dc removal mandatory
        dsp->decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);

        IQdc.sumIQx += x;
        IQdc.sumIQy += y;
        IQdc.cnt += 1;
        if (IQdc.cnt == IQdc.maxcnt) {
            IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
            IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
            IQdc.avgIQ  = IQdc.avgIQx + I*IQdc.avgIQy;
            IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            if (IQdc.maxcnt < IQdc.maxlim) IQdc.maxcnt *= 2;
        }
    }

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
        w[n] = 7938/18608.0 - 9240/18608.0*cos(_2PI*n/(taps-1)) + 1430/18608.0*cos(4*M_PI*n/(taps-1)); // Blackmann
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

static float complex lowpass1a(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    double complex w = 0;
    ui32_t n;
    ui32_t S = taps-1 + (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S-n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return (float complex)w;
// symmetry: ws[n] == ws[taps-1-n]
}
//static __attribute__((optimize("-ffast-math"))) float complex lowpass()
static float complex lowpass(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float complex w = 0;
    int n; // -Ofast
    int S = taps - (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S+n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return w;
// symmetry: ws[n] == ws[taps-1-n]
}
static float complex lowpass2(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float complex w = 0;     // -Ofast
    int n;
    int s = sample % taps; // lpIQ
    int S1 = s;
    int S1N = S1-taps;
    int n0 = taps-s;
    for (n = 0; n < n0; n++) {
        w += buffer[S1+n]*ws[n];
    }
    for (n = n0; n < taps; n++) {
        w += buffer[S1N+n]*ws[n];
    }
    return w;
// symmetry: ws[n] == ws[taps-1-n]
}

static float re_lowpass(float buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float w = 0;
    int n;
    int S = taps - (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S+n]; // ws[taps+s-n] = ws[(taps+sample-n)%taps]
    }
    return w;
}

static
int f32_sample(dsp_t *dsp, float *out) {
    float s = 0.0;
    float s_fm = s;

    float complex z, w, z0;
    double gain = FM_GAIN;

    ui32_t decFM = 1;
    ui32_t _sample = dsp->sample_in;
    int m = 0;

    if (dsp->opt_fmdec) {
        decFM = dsp->decFM;
        _sample = dsp->sample_in * decFM;
    }

    for (m = 0; m < decFM; m++)
    {
        double t = _sample / (double)dsp->sr;

        if (dsp->opt_iq)
        {
            if (dsp->opt_iq >= 5) {
                ui32_t s_reset = dsp->dectaps*dsp->lut_len;
                int j;
                if ( f32read_cblock(dsp) < dsp->decM ) return EOF;
                for (j = 0; j < dsp->decM; j++) {
                    if (dsp->opt_nolut) {
                        double _s_base = (double)(_sample*dsp->decM+j); // dsp->sample_dec
                        double f0 = dsp->xlt_fq*_s_base - dsp->Df*_s_base/(double)dsp->sr_base;
                        z = dsp->decMbuf[j] * cexp(f0*_2PI*I);
                    }
                    else {
                        z = dsp->decMbuf[j] * dsp->ex[dsp->sample_decM];
                    }
                    dsp->sample_decM += 1; if (dsp->sample_decM >= dsp->lut_len) dsp->sample_decM = 0;

                    dsp->decXbuffer[dsp->sample_decX] = z;
                    dsp->sample_decX += 1; if (dsp->sample_decX >= dsp->dectaps) dsp->sample_decX = 0;
                }
                if (dsp->decM > 1)
                {
                    z = lowpass(dsp->decXbuffer, dsp->sample_decX, dsp->dectaps, ws_dec);
                }
            }
            else if ( f32read_csample(dsp, &z) == EOF ) return EOF;

            if (dsp->opt_dc && !dsp->opt_nolut) {
                z *= cexp(-t*_2PI*dsp->Df*I);
            }


            // IF-lowpass
            if (dsp->opt_lp & LP_IQ) {
                dsp->lpIQ_buf[_sample % dsp->lpIQtaps] = z;
                z = lowpass(dsp->lpIQ_buf, _sample+1, dsp->lpIQtaps, dsp->ws_lpIQ);
            }


            z0 = dsp->iqbuf[(_sample-1) & 1];  // z0 = dsp->rot_iqbuf[(_sample-1 + dsp->N_IQBUF) % dsp->N_IQBUF];
            w = z * conj(z0);
            s_fm = gain * carg(w)/M_PI;

            dsp->iqbuf[_sample & 1] = z;  // dsp->rot_iqbuf[_sample % dsp->N_IQBUF] = z;


            s = s_fm; //opt_iq=1,6
        }
        else {
            if (f32read_sample(dsp, &s) == EOF) return EOF;
            s_fm = s; //opt_iq==0
        }

        // FM-lowpass
        if (dsp->opt_lp & LP_FM) {
            dsp->lpFM_buf[_sample % dsp->lpFMtaps] = s_fm;
            if (m+1 == decFM) {
                s_fm = re_lowpass(dsp->lpFM_buf, _sample+1, dsp->lpFMtaps, dsp->ws_lpFM);
                if (dsp->opt_iq < 2 || dsp->opt_iq > 5) s = s_fm; //opt_iq==0,1,6
            }
        }

        _sample += 1;

    }

    if (dsp->opt_dc && !dsp->opt_iq)
    {
        s -= dsp->dc*0.4;
    }


    dsp->bufs[dsp->sample_in % dsp->M] = s;


    if (dsp->opt_dc)
    {
        float xneu, xalt;
        xneu = dsp->bufs[ dsp->sample_in % dsp->M];
        xalt = dsp->bufs[(dsp->sample_in+1) % dsp->M];
        dsp->xsum +=  xneu - xalt;

        if ((dsp->sample_in+dsp->pre_pos) % dsp->sr == 0)
        {
            double dc = dsp->xsum / (double)dsp->M;
            dsp->dc = dc;
            dsp->dDf = dsp->sr * dsp->dc / (2.0*FM_GAIN);  // remaining freq offset
            dsp->Df += dsp->dDf*0.5;

            if (dsp->opt_iq) {
                if (fabs(dsp->dDf) > 2e3) {
                    if (dsp->locked) {
                        dsp->locked = 0;
                        dsp->ws_lpIQ = dsp->ws_lpIQ0;
                    }
                }
                else {
                    if (dsp->locked == 0) {
                        dsp->locked = 1;
                        dsp->ws_lpIQ = dsp->ws_lpIQ1;
                    }
                }
            }
            //DBG: if (dsp->opt_iq) fprintf(stderr, "Df: %+.3f\n", dsp->Df);
        }
    }

    dsp->sample_in += 1;

    *out = s;

    return 0;
}


/* -------------------------------------------------------------------------- */


#define IF_SAMPLE_RATE      48000
#define IF_SAMPLE_RATE_MIN  32000

#define IF_TRANSITION_BW (4e3)  // (min) transition width
#define FM_TRANSITION_BW (2e3)  // (min) transition width


static
int init_buffers(dsp_t *dsp) {
    int i, pos;
    float b0, b1, b2, b;
    double t;
    int n, k;


    // decimate
    if (dsp->opt_iq >= 5)
    {
        int IF_sr = IF_SAMPLE_RATE; // designated IF sample rate
        int decM = 1; // decimate M:1
        int sr_base = dsp->sr;
        float f_lp; // dec_lowpass: lowpass_bandwidth/2
        float t_bw; // dec_lowpass: transition_bandwidth
        int taps; // dec_lowpass: taps

        if (dsp->opt_IFmin) IF_sr = IF_SAMPLE_RATE_MIN;
        if (dsp->opt_imet1) IF_sr *= 2;

        if (IF_sr > sr_base) IF_sr = sr_base;
        if (IF_sr < sr_base) {
            while (sr_base % IF_sr) IF_sr += 1;
            decM = sr_base / IF_sr;
        }

        f_lp = (IF_sr+20e3)/(4.0*sr_base);
        t_bw = (IF_sr-20e3)/*/2.0*/;
        if (dsp->opt_imet1) {
            f_lp = (IF_sr+80e3)/(4.0*sr_base);
            t_bw = (IF_sr-80e3)/*/2.0*/;
        }
        if (dsp->opt_IFmin) {
            t_bw = (IF_sr-12e3);
            if (dsp->opt_imet1) {
                f_lp = (IF_sr+60e3)/(4.0*sr_base);
                t_bw = (IF_sr-60e3)/2/*2.0*/;
            }
        }
        if (t_bw < 0) t_bw = 10e3;
        t_bw /= sr_base;
        taps = 4.0/t_bw; if (taps%2==0) taps++;

        taps = lowpass_init(f_lp, taps, &ws_dec); // decimate lowpass
        if (taps < 0) return -1;
        dsp->dectaps = (ui32_t)taps;

        dsp->sr_base = sr_base;
        dsp->sr = IF_sr; // sr_base/decM
        dsp->sps /= (float)decM;
        dsp->decM = decM;

        fprintf(stderr, "IF: %d\n", IF_sr);
        fprintf(stderr, "dec: %d\n", decM);
    }
    if (dsp->opt_iq >= 5)
    {
        if (!dsp->opt_nolut)
        {
            // look up table, exp-rotation
            int W = 2*8; // 16 Hz window
            int d = 1; // 1..W , groesster Teiler d <= W von sr_base
            int freq = (int)( dsp->xlt_fq * (double)dsp->sr_base + 0.5);
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
                dsp->ex[n] = cexp(t*_2PI*I);
            }
        }

        dsp->decXbuffer = calloc( dsp->dectaps+1, sizeof(float complex));
        if (dsp->decXbuffer == NULL) return -1;

        dsp->decMbuf = calloc( dsp->decM+1, sizeof(float complex));
        if (dsp->decMbuf == NULL) return -1;
    }

    // IF lowpass
    if (dsp->opt_iq && (dsp->opt_lp & LP_IQ))
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        f_lp = 24e3/(float)dsp->sr/2.0; // default
        if (dsp->lpIQ_bw) f_lp = dsp->lpIQ_bw/(float)dsp->sr/2.0;
        taps = 4*dsp->sr/IF_TRANSITION_BW;
        //if (dsp->sr > 80e3) taps = taps/2;
        if (taps%2==0) taps++;
        taps = lowpass_init(1.5*f_lp, taps, &dsp->ws_lpIQ0); if (taps < 0) return -1;
        taps = lowpass_init(f_lp, taps, &dsp->ws_lpIQ1); if (taps < 0) return -1;

        dsp->lpIQ_fbw = f_lp;
        dsp->lpIQtaps = taps;
        dsp->lpIQ_buf = calloc( dsp->lpIQtaps+3, sizeof(float complex));
        if (dsp->lpIQ_buf == NULL) return -1;

        dsp->ws_lpIQ = dsp->ws_lpIQ1;
        // dc-offset: if not centered, (acquisition) filter bw = lpIQ_bw + 4kHz
        // coarse acquisition:
        if (dsp->opt_dc) {
            dsp->locked = 0;
            dsp->ws_lpIQ = dsp->ws_lpIQ0;
        }
    }

    // FM lowpass
    if (dsp->opt_lp & LP_FM)
    {
        float f_lp; // lowpass_bw
        int taps; // lowpass taps: 4*sr/transition_bw

        f_lp = 10e3/(float)dsp->sr; // default
        if (dsp->lpFM_bw > 0) f_lp = dsp->lpFM_bw/(float)dsp->sr;
        taps = 4*dsp->sr/FM_TRANSITION_BW; if (taps%2==0) taps++;
        if (dsp->decFM > 1)
        {
            f_lp *= 2; //if (dsp->opt_iq >= 2 && dsp->opt_iq < 6) f_lp *= 2;
            taps = taps/2;
        }
        if (dsp->sr > 100e3) taps = taps/2;
        if (taps%2==0) taps++;
        taps = lowpass_init(f_lp, taps, &dsp->ws_lpFM); if (taps < 0) return -1;

        dsp->lpFMtaps = taps;
        dsp->lpFM_buf = calloc( dsp->lpFMtaps+3, sizeof(float)); // re_lowpass: size(float)  (complex)lowpass: sizeof(float complex)
        if (dsp->lpFM_buf == NULL) return -1;
    }


    memset(&IQdc, 0, sizeof(IQdc));
    IQdc.maxlim = dsp->sr;
    IQdc.maxcnt = IQdc.maxlim/32; // 32,16,8,4,2,1
    if (dsp->decM > 1) {
        IQdc.maxlim *= dsp->decM;
        IQdc.maxcnt *= dsp->decM;
    }


    dsp->sample_in = 0;
    dsp->M = dsp->sps*32;  // a) dec buffer , b) len average/dc

    dsp->bufs = (float *)calloc( dsp->M+1, sizeof(float)); if (dsp->bufs  == NULL) return -100;

    if (dsp->opt_iq)
    {
        if (dsp->nch < 2) return -1;
    }


    return 0;
}

static
int free_buffers(dsp_t *dsp) {

    if (dsp->bufs)  { free(dsp->bufs);  dsp->bufs  = NULL; }

    // decimate
    if (dsp->opt_iq >= 5)
    {
        if (dsp->decXbuffer) { free(dsp->decXbuffer); dsp->decXbuffer = NULL; }
        if (dsp->decMbuf)    { free(dsp->decMbuf);    dsp->decMbuf    = NULL; }
        if (!dsp->opt_nolut) {
            if (dsp->ex)     { free(dsp->ex);         dsp->ex         = NULL; }
        }

        if (ws_dec) { free(ws_dec); ws_dec = NULL; }
    }

    // IF lowpass
    if (dsp->opt_iq && (dsp->opt_lp & LP_IQ))
    {
        if (dsp->ws_lpIQ0) { free(dsp->ws_lpIQ0); dsp->ws_lpIQ0 = NULL; }
        if (dsp->ws_lpIQ1) { free(dsp->ws_lpIQ1); dsp->ws_lpIQ1 = NULL; }
        if (dsp->lpIQ_buf) { free(dsp->lpIQ_buf); dsp->lpIQ_buf = NULL; }
    }
    // FM lowpass
    if (dsp->opt_lp & LP_FM)
    {
        if (dsp->ws_lpFM)  { free(dsp->ws_lpFM);  dsp->ws_lpFM  = NULL; }
        if (dsp->lpFM_buf) { free(dsp->lpFM_buf); dsp->lpFM_buf = NULL; }
    }

    return 0;
}


/* ------------------------------------------------------------------------------------ */


// Bell202, 1200 baud (1200Hz/2200Hz), 8N1
#define BAUD_RATE 1200

#define BITS (10)
#define LEN_BITFRAME  BAUD_RATE
#define LEN_BYTEFRAME  (LEN_BITFRAME/BITS)
#define HEADLEN 30

typedef struct {
    // GPS
    int hour;
    int min;
    int sec;
    float lat;
    float lon;
    int alt;
    int sats;
    float vH; float vD; float vV; // eGPS
    // PTU
    int frame;
    float temp;
    float pressure;
    float humidity;
    float batt;
    // XDATA
    char xdata[2*LEN_BYTEFRAME+1]; // xdata hex string: aux_str1#aux_str2...
    char *paux;
    //
    int gps_valid;
    int ptu_valid;
    //
    int jsn_freq;   // freq/kHz (SDR)
} gpx_t;

gpx_t gpx;


char header[] = "1111111111111111111""10""10000000""1";
char buf[HEADLEN+1] = "x";
int bufpos = -1;

int    bitpos;
ui8_t  bitframe[LEN_BITFRAME+1] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 1};
ui8_t  byteframe[LEN_BYTEFRAME+1];

int    N, ptr;
float *buffer = NULL;


/* ------------------------------------------------------------------------------------ */


void inc_bufpos() {
  bufpos = (bufpos+1) % HEADLEN;
}

int compare() {
    int i=0, j = bufpos;

    while (i < HEADLEN) {
        if (j < 0) j = HEADLEN-1;
        if (buf[j] != header[HEADLEN-1-i]) break;
        j--;
        i++;
    }
    return i;
}

int bits2byte(ui8_t *bits) {
    int i, d = 1, byte = 0;

    if ( bits[0]+bits[1]+bits[2]+bits[3]+bits[4] // 1 11111111 1 (sync)
        +bits[5]+bits[6]+bits[7]+bits[8]+bits[9] == 10 ) return 0xFFFF;

    for (i = 1; i < BITS-1; i++) {  // little endian
        if      (bits[i] == 1)  byte += d;
        else if (bits[i] == 0)  byte += 0;
        d <<= 1;
    }
    return byte & 0xFF;
}


int bits2bytes(ui8_t *bits, ui8_t *bytes, int len) {
    int i;
    int byte;
    for (i = 0; i < len; i++) {
        byte = bits2byte(bits+BITS*i);
        bytes[i] = byte & 0xFF;
        if (byte == 0xFFFF) break;
    }
    return i;
}

void print_rawbits(int len) {
    int i;
    for (i = 0; i < len; i++) {
        if ((i % BITS == 1) || (i % BITS == BITS-1)) fprintf(stdout, " ");
        fprintf(stdout, "%d", bitframe[i]);
    }
    fprintf(stdout, "\n");
}

int hexval(char nib) {
    int i;
    int h = 0;

    if      (nib >= '0' && nib <= '9') h = nib-'0';
    else if (nib >= 'a' && nib <= 'f') h = nib-'a'+0xA;
    else if (nib >= 'A' && nib <= 'F') h = nib-'A'+0xA;
    else return -1;

    return (h & 0xF);
}

/* -------------------------------------------------------------------------- */

int crc16poly = 0x1021; // CRC16-CCITT
int crc16(ui8_t bytes[], int len) {
    int rem = 0x1D0F;   // initial value
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

/* -------------------------------------------------------------------------- */

#define LEN_GPSePTU   (18+20)
/*
    standard frame:
    01 02 (GPS) .. .. 01 04 (ePTU) .. ..
*/

#define SOH_01     0x01

#define PKT_PTU    0x01
#define PKT_GPS    0x02
#define PKT_XDATA  0x03
#define PKT_ePTU   0x04
#define PKT_eGPS   0x05

/*
PTU (enhanced) Data Packet (LSB)
offset bytes description
 0     1     SOH = 0x01
 1     1     PKT_ID = 0x01/0x04
 2     2     PKT = packet number
 4     3     P, mbs (P = n/100)
 7     2     T, °C (T = n/100)
 9     2     U, % (U = n/100)
11     1     Vbat, V (V = n/10)
   12     2     Tint, °C (Tint = n/100)
   14     2     Tpr, °C (Tpr = n/100)
   16     2     Tu, °C (Tu = n/100)
12/18  2     CRC (16-bit)
packet size = 14/20 bytes
*/
#define pos_PCKnum  0x02  // 2 byte
#define pos_PTUprs  0x04  // 3 byte
#define pos_PTUtem  0x07  // 2 byte int
#define pos_PTUhum  0x09  // 2 byte
#define pos_PTUbat  0x0B  // 1 byte
#define pos_PTUcrc  0x0C  // 2 byte
#define pos_ePTUtint    0x0C  // 2 byte
#define pos_ePTUtpr     0x0E  // 2 byte
#define pos_ePTUtu      0x10  // 2 byte
#define pos_ePTUcrc     0x12  // 2 byte

int print_ePTU(int pos, ui8_t PKT_ID) {
    int P, U;
    short T;
    int bat, pcknum;
    int crc_val, crc;                    // 0x04: ePTU    0x01: PTU
    int posPTUCRC =  (PKT_ID == PKT_ePTU) ? pos_ePTUcrc : pos_PTUcrc;

    if (PKT_ID != PKT_ePTU && PKT_ID != PKT_PTU) return -1;

    crc_val = ((byteframe+pos)[posPTUCRC] << 8) | (byteframe+pos)[posPTUCRC+1];
    crc = crc16(byteframe+pos, posPTUCRC); // len=pos

    P   = (byteframe+pos)[pos_PTUprs] | ((byteframe+pos)[pos_PTUprs+1]<<8) | ((byteframe+pos)[pos_PTUprs+2]<<16);
    T   = (byteframe+pos)[pos_PTUtem] | ((byteframe+pos)[pos_PTUtem+1]<<8);
    U   = (byteframe+pos)[pos_PTUhum] | ((byteframe+pos)[pos_PTUhum+1]<<8);
    bat = (byteframe+pos)[pos_PTUbat];

    pcknum = (byteframe+pos)[pos_PCKnum] | ((byteframe+pos)[pos_PCKnum+1]<<8);
    fprintf(stdout, "[%d] ", pcknum);

    fprintf(stdout, " P:%.2fmb ", P/100.0);
    fprintf(stdout, " T:%.2f°C ", T/100.0);
    fprintf(stdout, " U:%.2f%% ", U/100.0);
    fprintf(stdout, " bat:%.1fV ", bat/10.0);

    fprintf(stdout, " # ");
    fprintf(stdout, " CRC: %04X ", crc_val);
    fprintf(stdout, "- %04X ", crc);
    if (crc_val == crc) {
        fprintf(stdout, "[OK]");
        gpx.ptu_valid = PKT_ID;
        gpx.frame = pcknum;
        gpx.pressure = P/100.0;
        gpx.temp = T/100.0;
        gpx.humidity = U/100.0;
        gpx.batt = bat/10.0;
    }
    else {
        fprintf(stdout, "[NO]");
        gpx.ptu_valid = 0;
    }
    fprintf(stdout, "\n");

    return (crc_val != crc);
}


/*
GPS (enhanced) Data Packet (LSB)
offset bytes description
 0     1     SOH = 0x01
 1     1     PKT_ID = 0x02/0x05
 2     4     Latitude, +/- deg (float)
 6     4     Longitude, +/- deg (float)
10     2     Altitude, meters (Alt = n-5000)
12     1     nSat (0 - 12)
   13     4     velE m/s (float)
   17     4     velN m/s (float)
   21     4     velU m/s (float)
13/25  3     Time (hr,min,sec)
16/28  2     CRC (16-bit)
packet size = 18/30 bytes
*/
#define pos_GPSlat  0x02  // 4 byte float
#define pos_GPSlon  0x06  // 4 byte float
#define pos_GPSalt  0x0A  // 2 byte int
#define pos_GPSsats 0x0C  // 1 byte
#define pos_GPStim  0x0D  // 3 byte
#define pos_GPScrc  0x10  // 2 byte
#define pos_eGPSvE      0x0D  // 4 byte float
#define pos_eGPSvN      0x11  // 4 byte float
#define pos_eGPSvU      0x15  // 4 byte float
#define pos_eGPStim     0x19  // 3 byte
#define pos_eGPScrc     0x1C  // 2 byte

int print_eGPS(int pos, ui8_t PKT_ID) {
    float lat, lon;
    float vE, vN, vU, vH, vD; // E,N,U, speed, dir/heading
    int alt, sats;
    int std, min, sek;
    int crc_val, crc;                // 0x02: GPS    0x05: eGPS
    int posGPStim =  (PKT_ID == PKT_GPS) ? pos_GPStim : pos_eGPStim;
    int posGPSCRC =  (PKT_ID == PKT_GPS) ? pos_GPScrc : pos_eGPScrc;

    if (PKT_ID != PKT_GPS && PKT_ID != PKT_eGPS) return -1;

    crc_val = ((byteframe+pos)[pos_GPScrc] << 8) | (byteframe+pos)[pos_GPScrc+1];
    crc = crc16(byteframe+pos, pos_GPScrc); // len=pos

    //lat = *(float*)(byteframe+pos+pos_GPSlat);
    //lon = *(float*)(byteframe+pos+pos_GPSlon);
    // //raspi: copy into (aligned) float
    memcpy(&lat, byteframe+pos+pos_GPSlat, 4);
    memcpy(&lon, byteframe+pos+pos_GPSlon, 4);

    alt = ((byteframe+pos)[pos_GPSalt+1]<<8)+(byteframe+pos)[pos_GPSalt] - 5000;
    sats = (byteframe+pos)[pos_GPSsats];
    std = (byteframe+pos)[posGPStim+0];
    min = (byteframe+pos)[posGPStim+1];
    sek = (byteframe+pos)[posGPStim+2];

    if (std < 25 && min < 61 && sek < 100) {
        fprintf(stdout, "(%02d:%02d:%02d) ", std, min, sek);
    }
    if (lat > -91.0f && lat < 91.0f && lon > -181.0f && lon < 181.0f) {
        fprintf(stdout, " lat: %.6f° ", lat);
        fprintf(stdout, " lon: %.6f° ", lon);
        if (alt > -1000 && alt < 80000) {
            fprintf(stdout, " alt: %dm ", alt);
        }
        fprintf(stdout, " sats: %d ", sats);
    }

    gpx.vH = gpx.vD = gpx.vV = 0;
    if (PKT_ID == PKT_eGPS) {
        memcpy(&vE, byteframe+pos+pos_eGPSvE, 4);
        memcpy(&vN, byteframe+pos+pos_eGPSvN, 4);
        memcpy(&vU, byteframe+pos+pos_eGPSvU, 4);
        vH = sqrt(vE*vE+vN*vN);
        vD = atan2(vE, vN) * 180.0 / M_PI;
        if (vD < 0) vD += 360.0;
        // TODO: TEST eGPS/vel
        if (vH < 1000.0f && vU > -1000.0f && vU < 1000.0f) {
            fprintf(stdout, "  vH: %.1fm/s  D: %.1f°  vV: %.1fm/s ", vH, vD, vU);
        }
    }

    fprintf(stdout, " # ");
    fprintf(stdout, " CRC: %04X ", crc_val);
    fprintf(stdout, "- %04X ", crc);
    if (crc_val == crc) {
        fprintf(stdout, "[OK]");
        gpx.gps_valid = PKT_ID;
        gpx.lat = lat;
        gpx.lon = lon;
        gpx.alt = alt;
        gpx.sats = sats;
        gpx.hour = std;
        gpx.min = min;
        gpx.sec = sek;
        if (PKT_ID == PKT_eGPS) {
            gpx.vH = vH;
            gpx.vD = vD;
            gpx.vV = vU;
        }
    }
    else {
        fprintf(stdout, "[NO]");
        gpx.gps_valid = 0;
    }
    fprintf(stdout, "\n");

    return (crc_val != crc);
}


/*
Extra Data Packet - XDATA
offset bytes description
 0     1     SOH = 0x01
 1     1     PKT_ID = 0x03
 2     2     N = number of data bytes to follow
 3+N   2     CRC (16-bit)
N=8, ID=0x01: ECC Ozonesonde (MSB)
 3     1     Instrument_type = 0x01 (ID)
 4     1     Instrument_number
 5     2     Icell, uA (I = n/1000)
 7     2     Tpump, °C (T = n/100)
 9     1     Ipump, mA
10     1     Vbat, (V = n/10)
11     2     CRC (16-bit)
packet size = 12 bytes
//
ID=0x05: OIF411
ID=0x08: CFH (Cryogenic Frost-Point Hygrometer)
ID=0x19: COBALD (Compact Optical Backscatter Aerosol Detector)
*/

int print_xdata(int pos, ui8_t N) {
    ui8_t InstrumentNum;
    short Tpump;
    unsigned short Icell, Ipump, Vbat;
    int crc_val, crc;
    int crc_len = 3+N;

    crc_val = ((byteframe+pos)[crc_len] << 8) | (byteframe+pos)[crc_len+1];
    crc = crc16(byteframe+pos, crc_len); // len=pos

    fprintf(stdout, " XDATA ");
    // (byteframe+pos)[2] = N
    if (N == 8 && (byteframe+pos)[3] == 0x01)
    {   // Ozonesonde 01 03 08 01 .. ..    (MSB)
        InstrumentNum = (byteframe+pos)[4];
        Icell = (byteframe+pos)[5+1] | ((byteframe+pos)[5]<<8); // MSB
        Tpump = (byteframe+pos)[7+1] | ((byteframe+pos)[7]<<8); // MSB
        Ipump = (byteframe+pos)[9];
        Vbat  = (byteframe+pos)[10];
        fprintf(stdout, " Icell:%.3fuA ", Icell/1000.0);
        fprintf(stdout, " Tpump:%.2f°C ", Tpump/100.0);
        fprintf(stdout, " Ipump:%dmA ", Ipump);
        fprintf(stdout, " Vbat:%.1fV ", Vbat/10.0);
    }
    else {
        int j;
        fprintf(stdout, " (N=0x%02X)", N);
        for (j = 0; j < N; j++) fprintf(stdout, " %02X", (byteframe+pos)[3+j]);
    }

    if (crc_val == crc && (gpx.paux-gpx.xdata)+2*(N+1) < 2*LEN_BYTEFRAME) {
        // hex(xdata[2:3+N]) , strip [0103NN]..[CRC16] , '#'-separated
        int j;
        if (gpx.paux > gpx.xdata) {
            *(gpx.paux) = '#';
            gpx.paux += 1;
        }
        //exclude length (byteframe+pos)[2]=N (sprintf(gpx.paux, "%02X", (byteframe+pos)[2]); gpx.paux += 2;)
        for (j = 0; j < N; j++) {
            sprintf(gpx.paux, "%02X", (byteframe+pos)[3+j]);
            gpx.paux += 2;
        }
        *(gpx.paux) = '\0';
    }

    fprintf(stdout, " # ");
    fprintf(stdout, " CRC: %04X ", crc_val);
    fprintf(stdout, "- %04X ", crc);
    if (crc_val == crc) {
        fprintf(stdout, "[OK]");
    }
    else {
        fprintf(stdout, "[NO]");
    }
    fprintf(stdout, "\n");

    return (crc_val != crc);
}


/* -------------------------------------------------------------------------- */


int print_frame(int len, int bits2byte) {
    int i;
    int framelen;
    int crc_err1 = 0,
        crc_err2 = 0,
        crc_err3 = 0;
    int ofs = 0;
    int out = 0;

    gpx.gps_valid = 0;
    gpx.ptu_valid = 0;

    if ( len < 2 || len > LEN_BYTEFRAME ) return -1;

    if (bits2byte) {
        memset(byteframe, 0, LEN_BYTEFRAME);
        framelen = bits2bytes(bitframe, byteframe, len);
    }
    else {
        framelen = len;
    }

    if (option_rawbits && !rawhex)
    {
        print_rawbits(framelen*BITS);
    }
    else
    {
        if (option_raw) {
            for (i = 0; i < framelen; i++) { // LEN_GPSePTU
                fprintf(stdout, "%02X ", byteframe[i]);
            }
            fprintf(stdout, "\n");
            out |= 8;
        }
        //else
        {
            ofs = 0;
            gpx.xdata[0] = '\0';
            gpx.paux = gpx.xdata;
            while (ofs < framelen && byteframe[ofs] == SOH_01) // SOH = 0x01
            {
                ui8_t PKT_ID = byteframe[ofs+1];
                if (PKT_ID == PKT_GPS || PKT_ID == PKT_eGPS) // GPS/eGPS Data Packet
                {
                    int posGPSCRC =  (PKT_ID == PKT_GPS) ? pos_GPScrc : pos_eGPScrc;
                    crc_err1 = print_eGPS(ofs, PKT_ID);  // packet offset in byteframe
                    ofs += posGPSCRC+2;
                    out |= 1;
                }
                else if (PKT_ID == PKT_ePTU || PKT_ID == PKT_PTU) // ePTU/PTU Data Packet
                {
                    int posPTUCRC =  (PKT_ID == PKT_ePTU) ? pos_ePTUcrc : pos_PTUcrc;
                    crc_err2 = print_ePTU(ofs, PKT_ID);  // packet offset in byteframe
                    ofs += posPTUCRC+2;
                    out |= 2;
                }
                else if (PKT_ID == PKT_XDATA) // Extra Data Packet
                {
                    ui8_t N = byteframe[ofs+2];
                    if (N > 0 && ofs+2+N+2 < framelen)
                    {
                        crc_err3 = print_xdata(ofs, N);  // packet offset in byteframe
                        ofs += N+3+2;
                        out |= 4;
                    }
                    else {
                        break;
                    }
                }
                else {
                    break;
                }
            }

            // if (crc_err1==0 && crc_err2==0) { }


            if (option_json) {
                if (gpx.gps_valid && gpx.ptu_valid) // frameNb part of PTU-pck
                {
                    char *ver_jsn = NULL;
                    fprintf(stdout, "{ \"type\": \"%s\"", "IMET");
                    fprintf(stdout, ", \"frame\": %d, \"id\": \"iMet\", \"datetime\": \"%02d:%02d:%02dZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %d, \"sats\": %d, \"temp\": %.2f, \"humidity\": %.2f, \"pressure\": %.2f, \"batt\": %.1f",
                            gpx.frame, gpx.hour, gpx.min, gpx.sec, gpx.lat, gpx.lon, gpx.alt, gpx.sats, gpx.temp, gpx.humidity, gpx.pressure, gpx.batt);
                    // TODO: TEST eGPS/vel
                    if (0 && gpx.gps_valid == PKT_eGPS) {
                        fprintf(stdout, ", \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f", gpx.vH, gpx.vD, gpx.vV );
                    }
                    if (gpx.xdata[0]) {
                        fprintf(stdout, ", \"aux\": \"%s\"", gpx.xdata );
                    }
                    if (gpx.jsn_freq > 0) {
                        fprintf(stdout, ", \"freq\": %d", gpx.jsn_freq );
                    }

                    // Reference time/position
                    fprintf(stdout, ", \"ref_datetime\": \"%s\"", "GPS" ); // {"GPS", "UTC"} GPS-UTC=leap_sec
                    fprintf(stdout, ", \"ref_position\": \"%s\"", "MSL" ); // {"GPS", "MSL"} GPS=ellipsoid , MSL=geoid

                    #ifdef VER_JSN_STR
                        ver_jsn = VER_JSN_STR;
                    #endif
                    if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
                    fprintf(stdout, " }\n");
                }
            }

            if (out) fprintf(stdout, "\n");
            fflush(stdout);
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */

double complex F1sum = 0;
double complex F2sum = 0;

int main(int argc, char *argv[]) {

    FILE *fp;
    char *fpname;
    unsigned int sample_count;
    int i;
    int bit = 8, bit0 = 8;
    int pos = 0, pos0 = 0;
    double pos_bit = 0;
    int header_found = 0;
    double bitlen; // sample_rate/BAUD_RATE
    int len;
    double f1, f2;
    double complex iw1, iw2;

    int n;
    double t  = 0.0;
    double tn = 0.0;
    double x  = 0.0;
    double x0 = 0.0;

    double complex X0 = 0;
    double complex X  = 0;

    double xbit = 0.0;
    float s = 0.0;

    int bitbuf[3];


    float lpIQ_bw = 16e3;

    int option_iq = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_decFM = 0;
    int option_noLUT = 0;
    int option_iqdc = 0;
    int option_pcmraw = 0;
    int option_min = 0;
    int wavloaded = 0;
    int sel_wavch = 0;

    int k;

    int cfreq = -1;

    pcm_t pcm = {0};
    dsp_t dsp = {0};

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
        else if ( (strcmp(*argv, "--rawbits") == 0) ) {
            option_rawbits = 1;
        }
        else if ( (strcmp(*argv, "-b") == 0) ) {
            option_b = 1;
        }
        else if ( (strcmp(*argv, "--iq") == 0) ) { // fq baseband -> IF (rotate from and decimate)
            double fq = 0.0;                       // --iq <fq> , -0.5 < fq < 0.5
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq; // S(t) -> S(t)*exp(-f*2pi*I*t)
            option_iq = 6;
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { option_lp |= LP_IQ; }  // IQ lowpass
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 4.0 && bw < 256.0) lpIQ_bw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= LP_FM; }  // FM lowpass
        else if   (strcmp(*argv, "--decFM") == 0) {   // FM decimation
            option_decFM = 2;
        }
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--imet1") == 0) { dsp.opt_imet1 = 1; }  // iMet-1-RS bw=64k
        else if ( (strcmp(*argv, "--json") == 0) ) {
            option_json = 1;
        }
        else if ( (strcmp(*argv, "--jsn_cfq") == 0) ) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if (strcmp(*argv, "--rawhex") == 0) { rawhex = 1; }  // raw hex input
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
                fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;


    if (!rawhex) {

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

        gpx.jsn_freq = 0;
        if (cfreq > 0) {
            int fq_kHz = (cfreq - dsp.xlt_fq*pcm.sr + 500)/1e3;
            gpx.jsn_freq = fq_kHz;
        }

        // init dsp
        //
        dsp.fp = fp;
        dsp.sr = pcm.sr;
        dsp.bps = pcm.bps;
        dsp.nch = pcm.nch;
        dsp.ch = pcm.sel_ch;
        dsp.br = (float)BAUD_RATE;

        if (option_decFM) {
            if (option_iq == 5) option_lp |= LP_IQFM;
            else                option_lp |= LP_FM;
            if (dsp.sr > 60000) dsp.opt_fmdec = 1;
        }
        dsp.sps = (float)dsp.sr/dsp.br;
        dsp.decFM = 1;
        if (dsp.opt_fmdec) {
            dsp.decFM = option_decFM;
            while (dsp.sr % dsp.decFM > 0  &&  dsp.decFM > 1) dsp.decFM /= 2;
            dsp.sps /= (float)dsp.decFM;
        }

        if (dsp.opt_imet1) {
            if (lpIQ_bw < 60e3) lpIQ_bw = 80e3;
        }

        dsp.opt_iq = option_iq;
        dsp.opt_iqdc = option_iqdc;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = lpIQ_bw; // IF lowpass bandwidth
        dsp.lpFM_bw = 6e3; // FM audio lowpass
        if (option_iq == 6) dsp.lpFM_bw = 6e3;
        else if (option_iq == 5) dsp.lpFM_bw = 6e3;
        dsp.opt_dc = option_dc;
        dsp.opt_IFmin = option_min;

        // LUT faster, however frequency correction after decimation
        // LUT recommonded if decM > 2
        //
        if (option_noLUT && option_iq >= 5) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;

        init_buffers(&dsp); // free

        dsp.sr_fm = dsp.sr/dsp.decFM;

        bitlen = dsp.sr_fm/(double)BAUD_RATE;

        f1 = 2200.0;  // bit0: 2200Hz
        f2 = 1200.0;  // bit1: 1200Hz
        iw1 = _2PI*I*f1;
        iw2 = _2PI*I*f2;

        N = 2*bitlen + 0.5;
        buffer = calloc( N+1, sizeof(float)); if (buffer == NULL) return -1;

        ptr = -1; sample_count = -1;

        while (f32_sample(&dsp, &s) != EOF) {

            ptr++; sample_count++;
            if (ptr == N) ptr = 0;
            buffer[ptr] = s;

            n = bitlen;
            t = sample_count / (double)dsp.sr_fm;
            tn = (sample_count-n) / (double)dsp.sr_fm;

            x = buffer[sample_count % N];
            x0 = buffer[(sample_count - n + N) % N];

            // f1
            X0 = x0 * cexp(-tn*iw1); // alt
            X  = x  * cexp(-t *iw1); // neu
            F1sum +=  X - X0;

            // f2
            X0 = x0 * cexp(-tn*iw2); // alt
            X  = x  * cexp(-t *iw2); // neu
            F2sum +=  X - X0;

            xbit = cabs(F2sum) - cabs(F1sum);

            s = xbit / bitlen;


            if ( s < 0 ) bit = 0;  // 2200Hz
            else         bit = 1;  // 1200Hz

            bitbuf[sample_count % 3] = bit;

            if (header_found && option_b)
            {
                if (sample_count - pos_bit > bitlen+bitlen/5 + 3)
                {
                    int bitsum = bitbuf[0]+bitbuf[1]+bitbuf[2];
                    if (bitsum > 1.5) bit = 1; else bit = 0;

                    bitframe[bitpos] = bit;
                    bitpos++;
                    if (bitpos >= LEN_BITFRAME-200) {  // LEN_GPSePTU*BITS+40

                        print_frame(bitpos/BITS, 1);

                        bitpos = 0;
                        header_found = 0;
                    }
                    pos_bit += bitlen;
                }
            }
            else
            {
                if (bit != bit0) {

                    pos0 = pos;
                    pos = sample_count;  //sample_count-(N-1)/2

                    len =  (pos-pos0)/bitlen + 0.5;
                    for (i = 0; i < len; i++) {
                        inc_bufpos();
                        buf[bufpos] = 0x30 + bit0;

                        if (!header_found) {
                            if (compare() >= HEADLEN) {
                                header_found = 1;
                                bitpos = 10;
                                pos_bit = pos;
                                if (option_b) {
                                    bitframe[bitpos] = bit;
                                    bitpos++;
                                }
                                dsp.mv_pos = dsp.sample_in;
                                dsp.pre_pos = dsp.mv_pos - HEADLEN*dsp.sps;
                                if (dsp.pre_pos > dsp.mv_pos) dsp.pre_pos = 0;
                            }
                        }
                        else {
                            bitframe[bitpos] = bit0;
                            bitpos++;
                            if (bitpos >= LEN_BITFRAME-200) {  // LEN_GPSePTU*BITS+40

                                print_frame(bitpos/BITS, 1);

                                bitpos = 0;
                                header_found = 0;
                            }
                        }
                    }
                    bit0 = bit;
                }
            }
        }

        if (buffer) { free(buffer); buffer = NULL; }
        free_buffers(&dsp);

    }
    else { // rawhex
        char buffer_rawhex[3*(LEN_BYTEFRAME)+12];
        char *pbuf = NULL;
        int hi, lo;
        int len, j, n;

        gpx.jsn_freq = 0;
        if (cfreq > 0) {
            gpx.jsn_freq = cfreq/1e3;
        }

        while (1 > 0) {

            memset(byteframe, 0, LEN_BYTEFRAME);
            memset(buffer_rawhex, 0, 3*(LEN_BYTEFRAME)+6);
            pbuf = fgets(buffer_rawhex, 3*(LEN_BYTEFRAME)+6, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[3*(LEN_BYTEFRAME)] = '\0';
            len = strlen(buffer_rawhex);
            while (len > 0 && buffer_rawhex[len-1] <= ' ') len--;
            buffer_rawhex[len] = '\0';
            j = 0;
            n = 0;
            while (pbuf[j] && n < LEN_BYTEFRAME) {
                if (pbuf[j] == ' ') j++; // if/while
                hi = hexval(pbuf[j]); if (hi < 0) break;
                j++;
                lo = hexval(pbuf[j]); if (lo < 0) break;
                j++;
                byteframe[n] = (hi << 4) | lo;
                n++;
            }
            len = n;
            if (len > 10) {
                print_frame(len, 0);
            }
        }
    }

    fprintf(stdout, "\n");


    fclose(fp);

    return 0;
}

