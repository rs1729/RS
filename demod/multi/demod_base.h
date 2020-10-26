

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <pthread.h>

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef char  i8_t;
typedef short i16_t;
typedef int   i32_t;


#define MAX_FQ 5
static int blk_sz = 32; // const


typedef struct {
    int tn;
    int tn_bit;
    pthread_t tid;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    //pthread_rwlock_t *lock;
    int max_fq;
    double xlt_fq;
    float complex *blk;
    int used;
} thd_t;


typedef struct {
    int sr;       // sample_rate
    int LOG2N;
    int N;
    int N2;
    float *xn;
    float complex  *ew;
    float complex  *Fm;
    float complex  *X;
    float complex  *Z;
    float complex  *cx;
    float complex  *win; // float real
} dft_t;


typedef struct {
    FILE *fp;
    //
    int sr;       // sample_rate
    int bps;      // bits/sample
    int nch;      // channels
    int ch;       // select channel
    //
    int symlen;
    int symhd;
    float sps;    // samples per symbol
    float _spb;   // samples per bit
    float br;     // baud rate
    //
    ui32_t sample_in;
    ui32_t sample_out;
    ui32_t delay;
    ui32_t sc;
    int buffered;
    int L;
    int M;
    int K;
    float *match;
    float *bufs;
    float mv;
    ui32_t mv_pos;
    ui32_t last_detect;
    //
    int N_norm;
    int Nvar;
/*
    float xsum;
    float qsum;
    float *xs;
    float *qs;
*/

    // IQ-data
    int opt_iq;
    int N_IQBUF;
    float complex *rot_iqbuf;
    float complex F1sum;
    float complex F2sum;

    //
    char *rawbits;
    char *hdr;
    int hdrlen;

    //
    float BT; // bw/time (ISI)
    float h;  // modulation index

    // DFT
    dft_t DFT;

    // dc offset
    int opt_dc;
    int locked;
    double dc;
    double Df;
    double dDf;

    ui32_t sample_posframe;
    ui32_t sample_posnoise;

    double V_noise;
    double V_signal;
    double SNRdB;

    // decimate
    int decM;
    int blk_cnt;
    ui32_t sr_base;
    ui32_t dectaps;
    ui32_t sample_dec;
    ui32_t lut_len;
    float complex *decXbuffer;
    float complex *decMbuf;
    float complex *ex; // exp_lut

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
	float *fm_buffer;

    int opt_cnt;

    thd_t *thd;
} dsp_t;


typedef struct {
    int sr;       // sample_rate
    int bps;      // bits_sample  bits/sample
    int nch;      // channels
    int sel_ch;   // select wav channel
//
    int opt_IFmin;
    int sr_base;
    int decM;
    int dectaps;
    FILE *fp;
} pcm_t;


typedef struct {
    ui8_t hb;
    float sb;
} hsbit_t;


typedef struct {
    pcm_t pcm;
    thd_t thd;
    int option_jsn;
    int option_dc;
    int option_cnt;
    int jsn_freq;
} thargs_t;



float read_wav_header(pcm_t *);
int f32buf_sample(dsp_t *, int);
int read_slbit(dsp_t *, int*, int, int, int, float, int);
int read_softbit(dsp_t *, hsbit_t *, int, int, int, float, int );

int init_buffers(dsp_t *);
int free_buffers(dsp_t *);

ui32_t get_sample(dsp_t *);

int find_header(dsp_t *, float, int, int, int);

int decimate_init(float f, int taps);
int decimate_free(void);
int iq_dc_init(pcm_t *);

int reset_blockread(dsp_t *);


