
/*
 *  compile:
 *      gcc -Ofast iq_fm.c -lm -o iq_fm
 *
 *  usage:
 *      ./iq_fm [--lpbw <lp>] [- <sr> <bps>] [--bo <bps_out> [--wav] [iqfile]
 *          --lpbw <lp>    :  lowpass bw in kHz, default 12.0
 *          - <sr> <bps>   :  input raw IQ data
 *          --bo <bps_out> :  bps=8,16,32 output bps
 *          --wav          :  output wav header
 *          [iqfile] : wav IQ-file or raw data (no iqfile: stdin)
 *
 *  examples:
 *      ./iq_fm --lpbw 8.0 - 48000 16 --wav iq_data_48k16.raw > fm.wav
 *      cat iq_data_48k32.raw | ./iq_fm --lpbw 8.0 - 48000 32 > fm_48k32.raw
 *      ./iq_fm --lpbw 8.0 --wav iq_data.wav > fm.wav
 *
 *  author: zilog80
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif

#define FM_GAIN (0.8)


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef char  i8_t;
typedef short i16_t;
typedef int   i32_t;


typedef struct {

    FILE *fp;
    //
    int sr;       // sample_rate
    int bps;      // bits/sample
    int nch;      // channels
    int bps_out;

    ui32_t sample;

    // IF: lowpass
    int lpIQ_bw;
    float lpIQ_fbw;
    int lpIQtaps;
    float *ws_lpIQ;
    float complex *lpIQ_buf;

} dsp_t;


typedef struct {
    int sr;       // sample_rate
    int bps;      // bits_sample  bits/sample
    int nch;      // channels
    int bps_out;
    FILE *fp;
} pcm_t;


static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static float read_wav_header(pcm_t *pcm) {
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

static float write_wav_header(pcm_t *pcm) {
    FILE *fp = stdout;
    ui32_t sr  = pcm->sr;
    ui32_t bps = pcm->bps_out;
    ui32_t data = 0;

    fwrite("RIFF", 1, 4, fp);
    data = 0; // bytes-8=headersize-8+datasize
    fwrite(&data,  1, 4, fp);
    fwrite("WAVE", 1, 4, fp);

    fwrite("fmt ", 1, 4, fp);
    data = 16; if (bps == 32) data += 2;
    fwrite(&data,  1, 4, fp);

    if (bps == 32) data = 3; // IEEE float
    else           data = 1; // PCM
    fwrite(&data,  1, 2, fp);

    data = 1; // 1 channel
    fwrite(&data,  1, 2, fp);

    data = sr;
    fwrite(&data,  1, 4, fp);

    data = sr*bps/8;
    fwrite(&data,  1, 4, fp);

    data = (bps+7)/8;
    fwrite(&data,  1, 2, fp);

    data = bps;
    fwrite(&data,  1, 2, fp);

    if (bps == 32) {
        data = 0; // size of extension: 0
        fwrite(&data, 1, 2, fp);
    }

    fwrite("data", 1, 4, fp);
    data = 0xFFFFFFFF; // datasize unknown
    fwrite(&data,  1, 4, fp);

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

    *z = x + I*y;

    return 0;
}

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

#define IF_TRANSITION_BW (4e3)  // 4kHz transition width

static int init_buffers(dsp_t *dsp) {

    float f_lp; // lowpass_bw
    int taps; // lowpass taps: 4*sr/transition_bw

    // IF lowpass
    f_lp = 24e3; // default
    f_lp = dsp->lpIQ_bw/(float)dsp->sr/2.0;
    taps = 4*dsp->sr/IF_TRANSITION_BW; if (taps%2==0) taps++;
    taps = lowpass_init(f_lp, taps, &dsp->ws_lpIQ); if (taps < 0) return -1;

    dsp->lpIQ_fbw = f_lp;
    dsp->lpIQtaps = taps;
    dsp->lpIQ_buf = calloc( dsp->lpIQtaps+3, sizeof(float complex));
    if (dsp->lpIQ_buf == NULL) return -1;

    return 0;
}

static int free_buffers(dsp_t *dsp) {

    if (dsp->lpIQ_buf) { free(dsp->lpIQ_buf); dsp->lpIQ_buf = NULL; }
    if (dsp->ws_lpIQ)  { free(dsp->ws_lpIQ);  dsp->ws_lpIQ  = NULL; }

    return 0;
}

/* -------------------------------------------------------------------------- */

static int fm_demod(dsp_t *dsp, float *s) {
    double gain = FM_GAIN;

    float complex z, z1, w;
    static float complex z0;

    if ( f32read_csample(dsp, &z) == EOF ) return EOF;

    dsp->lpIQ_buf[dsp->sample % dsp->lpIQtaps] = z;
    z1 = lowpass(dsp->lpIQ_buf, dsp->sample, dsp->lpIQtaps, dsp->ws_lpIQ);

    w = z1 * conj(z0);
    *s = gain * carg(w)/M_PI;

    z0 = z1;

    dsp->sample += 1;

    return 0;
}

static int write_fm(dsp_t *dsp, float s) {
    int bps = dsp->bps_out;
    FILE *fpo = stdout;
    ui8_t u = 0;
    i16_t b = 0;
    ui32_t *w = (ui32_t*)&s;

    if (bps == 8) {
        s *= 127.0;
        s += 128.0;
        u = (ui8_t)s;
        w = (ui32_t*)&u;
    }
    else if (bps == 16) {
        s *= 127.0*256.0;
        b = (i16_t)s;
        w = (ui32_t*)&b;
    }
    fwrite( w, bps/8, 1, fpo);

    return 0;
}


int main(int argc, char *argv[]) {

    int k;
    int option_pcmraw = 0;
    int option_wav = 0;
    int file_loaded = 0;

    int bitQ = 0;

    float lpIQ_bw = 12e3;
    float s = 0.0;

    FILE *fp;

    pcm_t pcm = {0};
    dsp_t dsp = {0};


    ++argv;
    while ((*argv) && (!file_loaded)) {
        if (0) { }
        else if   (strcmp(*argv, "--lpbw") == 0) {  // IQ lowpass BW / kHz
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return 1;
            if (bw > 4.6 && bw < 24.0) lpIQ_bw = bw*1e3;
        }
        else if (strcmp(*argv, "-") == 0) {
            int sample_rate = 0, bits_sample = 0, channels = 0;
            ++argv;
            if (*argv) sample_rate = atoi(*argv); else return 1;
            ++argv;
            if (*argv) bits_sample = atoi(*argv); else return 1;
            channels = 2;
            if (sample_rate < 1 || (bits_sample != 8 && bits_sample != 16 && bits_sample != 32)) {
                fprintf(stderr, "- <sr> <bs>\n");
                return 1;
            }
            pcm.sr  = sample_rate;
            pcm.bps = bits_sample;
            pcm.nch = channels;
            option_pcmraw = 1;
        }
        else if (strcmp(*argv, "--bo") == 0) {
            int bps_out = 0;
            ++argv;
            if (*argv) bps_out = atoi(*argv); else return 1;
            if ((bps_out != 8 && bps_out != 16 && bps_out != 32)) {
                bps_out = 0;
            }
            pcm.bps_out = bps_out;
        }
        else if (strcmp(*argv, "--wav") == 0) {
            option_wav = 1;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error: open %s\n", *argv);
                return 1;
            }
            file_loaded = 1;
        }
        ++argv;
    }
    if (!file_loaded) fp = stdin;

    pcm.fp = fp;
    if (option_pcmraw == 0) {
        k = read_wav_header( &pcm );
        if ( k < 0 ) {
            fclose(fp);
            fprintf(stderr, "error: wav header\n");
            return 1;
        }
    }
    if (pcm.nch < 2) {
        fprintf(stderr, "error: iq channels < 2\n");
        return 1;
    }

    if (pcm.bps_out == 0) pcm.bps_out = pcm.bps;

    if (option_wav) write_wav_header( &pcm );

    dsp.fp = fp;
    dsp.sr  = pcm.sr;
    dsp.bps = pcm.bps;
    dsp.nch = pcm.nch;
    dsp.bps_out = pcm.bps_out;
    dsp.lpIQ_bw = lpIQ_bw;  // IF lowpass bandwidth

    init_buffers(&dsp);


    while ( 1 )
    {
        bitQ = fm_demod(&dsp, &s);
        if ( bitQ == EOF ) break;

        write_fm(&dsp, s);
    }


    free_buffers(&dsp);

    return 0;
}

