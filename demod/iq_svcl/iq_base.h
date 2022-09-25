

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


#define MAX_FQ (4+1)
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
    //
    int fft;
    int stop_fft;
    int fft_num;
} thd_t;


typedef struct {
    int sr;       // sample_rate
    int LOG2N;
    int N;
    int N2;
    float complex  *ew;
    float complex  *X;
    float complex  *Z;
    float complex  *win; // float real
} dft_t;


typedef struct {
    FILE *fp;
    //
    int sr;       // sample_rate
    int bps;      // bits/sample
    int nch;      // channels
    int bps_out;
    //

    // DFT
    dft_t DFT;

    // decimate
    int decM;
    int blk_cnt;
    ui32_t sr_base;
    ui32_t dectaps;
    ui32_t sample_decX;
    ui32_t lut_len;
    ui32_t sample_decM;
    float complex *decXbuffer;
    float complex *decMbuf;
    float complex *ex; // exp_lut

    int opt_dbg;

    thd_t *thd;
} dsp_t;


typedef struct {
    int sr;       // sample_rate
    int bps;      // bits_sample  bits/sample
    int nch;      // channels
    int bps_out;
    int opt_IFmin;
    int sr_base;
    int decM;
    int dectaps;
    FILE *fp;
} pcm_t;


typedef struct {
    pcm_t pcm;
    thd_t thd;
    int fd;
    char *fname;
    FILE *fpo;
} thargs_t;



int read_wav_header(pcm_t *);

int init_buffers(dsp_t *);
int free_buffers(dsp_t *);

int decimate_init(float f, int taps);
int decimate_free(void);
int iq_dc_init(pcm_t *);

int reset_blockread(dsp_t *);

int read_ifblock(dsp_t *dsp, float complex *z);
int read_fftblock(dsp_t *dsp);

void raw_dft(dft_t *dft, float complex *Z);
void db_power(dft_t *dft, float complex Z[], float db[]);
float bin2freq(dft_t *dft, int k);
float bin2fq(dft_t *dft, int k);


