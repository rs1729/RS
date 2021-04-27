
/*
 *  compile:
 *
 *      gcc -Ofast -c iq_base.c
 *      gcc -O2 iq_server.c iq_base.o -lm -pthread -o iq_server
 *
 *      (gcc -O2 iq_client.c -o iq_client)
 *
 *  usage:
 *      ./iq_server [--enable_clsv_out] [--port <pn>] [iq_baseband.wav]
 *                  no wav-file: stdin
 *
 *      ./iq_server [--bo <b>] - <sr> <bs> [iq_baseband.raw]
 *                  <b>=8,16,32 bit client/IF output
 *
 *      ./iq_server <fft_opt> <m> <filename> [iq_baseband.wav]  # FFT csv output
 *                  <fft_opt>:
 *                             --fft_avg
 *                             --fft_all
 *                  <m>:
 *                       _avg: m avg-FFTs
 *                       _all: m*FFT_FPS/2 FFTs
 *                       m = -1 : continuous FFT output
 *                  <filename>:
 *                              csv filename  ("-": stdout)
 *
 *  author: zilog80
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h> // open(),close()
#include <fcntl.h> // O_RDONLY //... setmode()/cygwin

#include <signal.h>

#ifdef CYGWIN
  #include <io.h>
#endif

#include "iq_svcl.h"
#include "iq_base.h"

#define FFT_AVG 2  // fft_avg: integrate FFT_AVG seconds, 2*FFT_FPS FFTs
#define FFT_FPS 16 // fft_all: output (ca.) FFT_FPS/2 per sec

#define FPOUT stderr

static int option_dbg = 0;
static int option_clsv_out = 0;

static int tcp_eof = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
//static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

static float complex *block_decMB;

extern int rbf1;   // iq_base.c
extern int bufeof; // iq_base.c

#define IF_SAMPLE_RATE      48000
#define IF_SAMPLE_RATE_MIN  32000

static int pcm_dec_init(pcm_t *p) {

    int IF_sr = IF_SAMPLE_RATE; // designated IF sample rate
    int decM = 1; // decimate M:1
    int sr_base = p->sr;
    float f_lp; // dec_lowpass: lowpass_bandwidth/2
    float tbw;  // dec_lowpass: transition_bandwidth/Hz
    int taps;   // dec_lowpass: taps

    if (p->opt_IFmin) IF_sr = IF_SAMPLE_RATE_MIN;
    if (IF_sr > sr_base) IF_sr = sr_base;
    if (IF_sr < sr_base) {
        while (sr_base % IF_sr) IF_sr += 1;
        decM = sr_base / IF_sr;
    }

    f_lp = (IF_sr+20e3)/(4.0*sr_base);
    tbw  = (IF_sr-20e3)/*/2.0*/;
    if (p->opt_IFmin) {
        tbw = (IF_sr-12e3);
    }
    if (tbw < 0) tbw = 10e3;
    taps = sr_base*4.0/tbw; if (taps%2==0) taps++;

    taps = decimate_init(f_lp, taps);

    if (taps < 0) return -1;
    p->dectaps = (ui32_t)taps;
    p->sr_base = sr_base;
    p->sr = IF_sr; // sr_base/decM
    p->decM = decM;
    if (p->bps_out == 0) p->bps_out = p->bps;

    iq_dc_init(p);

    fprintf(stderr, "IF : %d\n", IF_sr);
    fprintf(stderr, "dec: %d\n", decM);
    fprintf(stderr, "bps: %d\n", p->bps_out);

    if (option_dbg) {
        fprintf(stderr, "taps: %d\n", taps);
        fprintf(stderr, "transBW: %.4f = %.1f Hz\n", tbw/sr_base, tbw);
        fprintf(stderr, "f: +/-%.4f = +/-%.1f Hz\n", f_lp, f_lp*sr_base);
    }

    return 0;
}


static int write_cpx(dsp_t *dsp, int fd, float complex z) {
    short b[2];
    ui8_t u[2];
    float x, y;
    int bps = dsp->bps_out;

    x = creal(z);
    y = cimag(z);

    if (bps == 32) {
        write(fd, &x, bps/8);
        write(fd, &y, bps/8);
    }
    else {
        x *= 128.0; // 127.0
        y *= 128.0; // 127.0
        if (bps == 8) {
            x += 128.0; // x *= scale8b;
            u[0] = (ui8_t)(x); //b = (int)(x+0.5);
            y += 128.0; // x *= scale8b;
            u[1] = (ui8_t)(y); //b = (int)(y+0.5);
            write(fd, u, 2*bps/8);
        }
        else { // bps == 16
            x *= 256.0;
            y *= 256.0;
            b[0] = (short)x; //b = (int)(x+0.5);
            b[1] = (short)y; //b = (int)(y+0.5);
            write(fd, b, 2*bps/8);
        }
    }

    return 0;
}

static int write_cpx_blk(dsp_t *dsp, int fd, float complex *z, int len) {
    int j, l;
    short b[2*len];
    ui8_t u[2*len];
    float xy[2*len];
    int bps = dsp->bps_out;

    for (j = 0; j < len; j++) {
        xy[2*j  ] = creal(z[j]);
        xy[2*j+1] = cimag(z[j]);
    }

    if (bps == 32) {
        l = write(fd, xy, 2*len*bps/8);
    }
    else {
        for (j = 0; j < 2*len; j++) xy[j] *= 128.0; // 127.0
        if (bps == 8) {
            for (j = 0; j < 2*len; j++) {
                xy[j] += 128.0; // x *= scale8b;
                u[j] = (ui8_t)(xy[j]); //b = (int)(x+0.5);
            }
            l = write(fd, u, 2*len*bps/8);
        }
        else { // bps == 16
            for (j = 0; j < 2*len; j++) {
                xy[j] *= 256.0;
                b[j] = (short)xy[j]; //b = (int)(x+0.5);
            }
            l = write(fd, b, 2*len*bps/8);
        }
    }

    return l*8/(2*bps);
}


#define ZLEN 64
static void *thd_IF(void *targs) { // pcm_t *pcm, double xlt_fq

    thargs_t *tharg = targs;
    pcm_t *pcm = &(tharg->pcm);

    int k;
    int bitQ = 0;
    int n = 0;
    int len = ZLEN;
    int l;

    float complex z_vec[ZLEN];

    char msg[HDRLEN];

    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));


    // init dsp
    //
    dsp.fp = pcm->fp;
    dsp.sr = pcm->sr;
    dsp.bps = pcm->bps;
    dsp.nch = pcm->nch;
    dsp.sr_base = pcm->sr_base;
    dsp.dectaps = pcm->dectaps;
    dsp.decM = pcm->decM;

    dsp.thd = &(tharg->thd);

    dsp.opt_dbg = option_dbg;

    // default: bps_out = bps_in
    // bps_out < f32: IQ-dc offset (iq_client/decoder)
    // bps_out = 32 recommended
    dsp.bps_out = pcm->bps_out;


    if (option_dbg) {
        fprintf(stderr, "init IF buffers\n");
    }

    k = init_buffers(&dsp);
    if ( k < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        goto exit_thread;
    }


    // header
    //
    k = 0;
    memset(msg, 0, HDRLEN);
    snprintf(msg, HDRLEN, "client: %d\nsr: %d\nbsp: %d\n", (dsp.thd)->tn, dsp.sr, dsp.bps_out);
    k = strlen(msg);
    l = write(tharg->fd, msg, HDRLEN);


    bitQ = 0;
    while ( bitQ != EOF )
    {
        bitQ = read_ifblock(&dsp, z_vec+n);
        n++;
        if (n == len || bitQ == EOF) {
            if (bitQ == EOF) n--;
            l = write_cpx_blk(&dsp, tharg->fd, z_vec, n);
            if (option_dbg && l != n) {
                fprintf(stderr, "l: %d  n: %d\n", l, n);
            }
            if (l <= 0) {
                bitQ = read_ifblock(&dsp, z_vec+n);
                (dsp.thd)->used = 0;
            }
            n = 0;
        }

        if ( (dsp.thd)->used == 0 )
        {
            pthread_mutex_lock( (dsp.thd)->mutex );
            fprintf(FPOUT, "<%d: CLOSE>\n", (dsp.thd)->tn);
            pthread_mutex_unlock( (dsp.thd)->mutex );
            break;
        }
    }


    free_buffers(&dsp);

  exit_thread:

    reset_blockread(&dsp);
    (dsp.thd)->used = 0;

    close(tharg->fd);

    if (bitQ == EOF) {
        fprintf(stderr, "<%d: EOF>\n", (dsp.thd)->tn);
    }

    tcp_eof = (bitQ == EOF);

    return NULL;
}


static int fft_txt_prn(FILE *fpo, dft_t *dft, float *db) {
    int j;

    fprintf(fpo, "# <freq/sr>;<dB>  ##  sr:%d , N:%d\n", dft->sr, dft->N);
    for (j = dft->N/2; j < dft->N/2 + dft->N; j++) {
        fprintf(fpo, "%+11.8f;%7.2f\n", bin2fq(dft, j % dft->N), db[j % dft->N]);
    }

    return 0;
}

static int fft_txt_tcp(int fd, dft_t *dft, float *db) {
    char sendln[LINELEN+1];
    int sendln_len;
    int j, l;

    snprintf(sendln, LINELEN, "# <freq/sr>;<dB>  ##  sr:%d , N:%d\n", dft->sr, dft->N);
    sendln_len = strlen(sendln);
    l = write(fd, sendln, sendln_len);
    for (j = dft->N/2; j < dft->N/2 + dft->N; j++) {
        memset(sendln, 0, LINELEN+1);
        snprintf(sendln, LINELEN, "%+11.8f;%7.2f\n", bin2fq(dft, j % dft->N), db[j % dft->N]);
        sendln_len = strlen(sendln);
        l = write(fd, sendln, sendln_len);
    }

    return 0;
}


static int fft_csv_prn(FILE *fpo, dft_t *dft, float *db, double t_sec) {
    int j;

    fprintf(fpo, "%7.3f, ", t_sec);
    fprintf(fpo, "%d, %d, ", (int)bin2freq(dft, dft->N/2), (int)bin2freq(dft, dft->N/2 - 1));
    fprintf(fpo, "%.2f, ", dft->sr/(double)dft->N);
    fprintf(fpo, "%d, ", dft->N);
    for (j = dft->N/2; j < dft->N/2 + dft->N; j++) {
        fprintf(fpo, "%7.2f%c", db[j % dft->N], j < dft->N/2 + dft->N-1 ? ',' : '\n');
    }

    return 0;
}

static int fft_csv_tcp(int fd, dft_t *dft, float *db, double t_sec) {
    char sendln[LINELEN+1];
    int sendln_len;
    int j, l;

    snprintf(sendln, LINELEN, "%7.3f, %d, %d, %.2f, %d, ",
             t_sec, (int)bin2freq(dft, dft->N/2), (int)bin2freq(dft, dft->N/2 - 1), dft->sr/(double)dft->N, dft->N);
    sendln_len = strlen(sendln);
    l = write(fd, sendln, sendln_len);
    for (j = dft->N/2; j < dft->N/2 + dft->N; j++) {
        memset(sendln, 0, LINELEN+1);
        snprintf(sendln, LINELEN, "%7.2f%c", db[j % dft->N], j < dft->N/2 + dft->N-1 ? ',' : '\n');
        sendln_len = strlen(sendln);
        l = write(fd, sendln, sendln_len);
    }

    return 0;
}


static void *thd_FFT(void *targs) {

    thargs_t *tharg = targs;
    pcm_t *pcm = &(tharg->pcm);

    int k;
    int bitQ = 0;

    float complex *z = NULL;
    float *db     = NULL;
    float *all_rZ = NULL;
    float *avg_rZ = NULL;
    float *avg_db = NULL;

    dsp_t dsp = {0};  //memset(&dsp, 0, sizeof(dsp));


    // init dsp
    //
    dsp.fp = pcm->fp;
    dsp.sr = pcm->sr;
    dsp.bps = pcm->bps;
    dsp.nch = pcm->nch;
    dsp.sr_base = pcm->sr_base;
    dsp.dectaps = pcm->dectaps;
    dsp.decM = pcm->decM;

    dsp.thd = &(tharg->thd);

    dsp.opt_dbg = option_dbg;
    dsp.bps_out = pcm->bps_out;


    if (option_dbg) {
        fprintf(stderr, "init FFT buffers\n");
    }

    k = init_buffers(&dsp);
    if ( k < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        goto exit_thread;
    }


    z = calloc(dsp.decM+1, sizeof(float complex));  if (z  == NULL) goto exit_thread;

    db     = calloc(dsp.DFT.N+1, sizeof(float));  if (db     == NULL) goto exit_thread;
    all_rZ = calloc(dsp.DFT.N+1, sizeof(float));  if (all_rZ == NULL) goto exit_thread;
    avg_rZ = calloc(dsp.DFT.N+1, sizeof(float));  if (avg_rZ == NULL) goto exit_thread;
    avg_db = calloc(dsp.DFT.N+1, sizeof(float));  if (avg_db == NULL) goto exit_thread;


    int j, n = 0;
    int len = dsp.DFT.N / dsp.decM;
    int mlen = len*dsp.decM;
    int sum_n = 0;
    int sum_fft = 0;
    int avg_sec = FFT_AVG;
    int fft_step = (int)(dsp.sr_base/(double)(dsp.DFT.N*FFT_FPS) + 0.5);
    int n_fft = 0;
    int th_used = 0;
    int readSamples = 1;

    int n_out = 0;


    bitQ = 0;
    while ( bitQ != EOF )
    {
        #ifdef FFT_READ_SINK_MIN
        ////              // th_used = 0; for (j = 0; j < MAX_FQ; j++) th_used += tharg[j].thd.used;
        if (rbf1 == 0) {  // if (readSamples == 0 && th_used == 1) ...
            readSamples = 1;
            pthread_mutex_lock( (dsp.thd)->mutex );
            rbf1 |= (dsp.thd)->tn_bit;
            pthread_mutex_unlock( (dsp.thd)->mutex );
            n = 0;
            n_fft = 0;
            sum_n = 0;
        }
        ////
        #endif


        if ( readSamples )
        {
            bitQ = read_fftblock(&dsp);

            for (j = 0; j < dsp.decM; j++) dsp.DFT.Z[n*dsp.decM + j] = dsp.decMbuf[j];

            n++;
            if (n == len) { // mlen = len * decM <= DFT.N

                if ( (dsp.thd)->fft )
                {
                    if ((dsp.thd)->fft_num == 0)
                    {
                        if ( tharg->fpo ) { fclose(tharg->fpo); tharg->fpo = NULL; }
                        else if ( tharg->fd > STDIN_FILENO ) { close(tharg->fd); tharg->fd = -1; }

                        (dsp.thd)->fft = 0;
                    }

                    n_fft += 1;

                    if (sum_n*n_fft*mlen < avg_sec*dsp.sr_base && n_fft >= fft_step) {
                        n_fft = fft_step;

                        if (sum_fft == 0) {
                            pthread_mutex_lock( (dsp.thd)->mutex );
                            fprintf(FPOUT, "<%d: FFT_START>\n", (dsp.thd)->tn);
                            pthread_mutex_unlock( (dsp.thd)->mutex );
                        }

                        for (j = 0; j < mlen; j++) {
                            dsp.DFT.Z[j] *= dsp.DFT.win[j];
                        }
                        while (j < dsp.DFT.N) dsp.DFT.Z[j++] = 0.0; // dft(Z[...]) != 0

                        raw_dft(&(dsp.DFT), dsp.DFT.Z);

                        for (j = 0; j < dsp.DFT.N; j++) {
                            float rZ = cabs(dsp.DFT.Z[j]);
                            avg_rZ[j] += rZ;
                            all_rZ[j] += rZ;
                        }

                        if ( (sum_fft&1)==1 && ((dsp.thd)->fft & OPT_FFT_AVG) == 0 ) {
                            double t_sec = sum_fft*n_fft*mlen / (double)dsp.sr_base;
                            for (j = 0; j < dsp.DFT.N; j++) {                      // if sum_fft odd,
                                db[j] = 20.0*log10(0.5*all_rZ[j]/dsp.DFT.N+1e-20); // 0.5: rZ_0+rZ_1
                                all_rZ[j] = 0.0f;
                            }
                            // sec.ms, freq_min, freq_max, Hz/bin, N_bins, db_1, ..., db_N
                            if ( tharg->fpo ) { // save FFT at server
                                fft_csv_prn(tharg->fpo, &(dsp.DFT), db, t_sec);
                            }
                            else if ( tharg->fd > STDIN_FILENO ) {
                                fft_csv_tcp(tharg->fd, &(dsp.DFT), db, t_sec);
                            }
                        }

                        sum_n++;
                        sum_fft++;
                        n_fft = 0;
                    }
                    if (sum_n*fft_step*mlen >= avg_sec*dsp.sr_base) {
                        float nN = 1.0/(dsp.DFT.N*(float)sum_n);
                        for (j = 0; j < dsp.DFT.N; j++) {
                            avg_db[j] = 20.0*log10(nN*avg_rZ[j]+1e-20);
                            avg_rZ[j] = 0.0f;
                        }

                        if ( (dsp.thd)->fft & OPT_FFT_AVG ) {
                            double t_sec = sum_fft*fft_step*mlen / (double)dsp.sr_base;
                            if ( tharg->fpo ) { // send FFT data to client
                                fft_csv_prn(tharg->fpo, &(dsp.DFT), avg_db, t_sec);
                            }
                            else if ( tharg->fd > STDIN_FILENO ) {
                                fft_csv_tcp(tharg->fd, &(dsp.DFT), avg_db, t_sec);
                            }
                        }

                        n_out++;

                        if ((dsp.thd)->fft_num > 0 && n_out >= (dsp.thd)->fft_num)
                        {
                            if ( tharg->fpo ) { fclose(tharg->fpo); tharg->fpo = NULL; }
                            else if ( tharg->fd > STDIN_FILENO ) { close(tharg->fd); tharg->fd = -1; }

                            (dsp.thd)->fft = 0;
                            sum_fft = 0;
                            n_out = 0;

                            pthread_mutex_lock( (dsp.thd)->mutex );
                            fprintf(FPOUT, "<%d: FFT_STOP>\n", (dsp.thd)->tn);
                            pthread_mutex_unlock( (dsp.thd)->mutex );

                        }

                        sum_n = 0;
                    }
                }

                #ifdef FFT_READ_SINK_MIN
                ////
                th_used = 0;
                for (j = 0; j < MAX_FQ; j++) th_used += tharg[j].thd.used;
                if (th_used > 1) {
                    readSamples = 0;
                    reset_blockread(&dsp);
                }
                ////
                #endif

                n = 0;
            }
        }

        if ( (dsp.thd)->used == 0 )
        {
            (dsp.thd)->fft = 0;
            pthread_mutex_lock( (dsp.thd)->mutex );
            fprintf(FPOUT, "<%d: CLOSE>\n", (dsp.thd)->tn);
            pthread_mutex_unlock( (dsp.thd)->mutex );
            break;
        }

        if ( (dsp.thd)->stop_fft > 0 )
        {
            if ( tharg->fpo ) { fclose(tharg->fpo); tharg->fpo = NULL; }
            else if ( tharg->fd > STDIN_FILENO ) { close(tharg->fd); tharg->fd = -1; }

            (dsp.thd)->fft = 0;
            (dsp.thd)->stop_fft = 0;
            sum_fft = 0;
            sum_n = 0;
            n_out = 0;

            pthread_mutex_lock( (dsp.thd)->mutex );
            fprintf(FPOUT, "<%d: STOP_FFT>\n", (dsp.thd)->tn);
            pthread_mutex_unlock( (dsp.thd)->mutex );
        }
    }

    if ((dsp.thd)->fft_num < 0)
    {
        if ( tharg->fpo ) { fclose(tharg->fpo); tharg->fpo = NULL; }
        else if ( tharg->fd > STDIN_FILENO ) { close(tharg->fd); tharg->fd = -1; }
        (dsp.thd)->fft = 0;

        pthread_mutex_lock( (dsp.thd)->mutex );
        fprintf(FPOUT, "<%d: FFT_STOP>\n", (dsp.thd)->tn);
        pthread_mutex_unlock( (dsp.thd)->mutex );
    }

    if (bitQ == EOF) {
        pthread_mutex_lock( (dsp.thd)->mutex );
        fprintf(FPOUT, "<%d: EOF>\n", (dsp.thd)->tn);
        pthread_mutex_unlock( (dsp.thd)->mutex );
    }


    free_buffers(&dsp);

  exit_thread:

    if (z) { free(z); z = NULL; }
    if (db) { free(db); db = NULL; }
    if (all_rZ) { free(all_rZ); all_rZ = NULL; }
    if (avg_rZ) { free(avg_rZ); avg_rZ = NULL; }
    if (avg_db) { free(avg_db); avg_db = NULL; }

    reset_blockread(&dsp);
    (dsp.thd)->used = 0;

    tcp_eof = (bitQ == EOF);

    return NULL;
}


int main(int argc, char **argv) {

    FILE *fp = NULL;
    int wavloaded = 0;
    int k;
    int xlt_cnt = 0;
    double base_fqs[MAX_FQ];
    void *rstype[MAX_FQ];
    int option_pcmraw = 0,
        option_min = 0;
    char *fname_fft = "db_fft.txt";

    // TCP
    sa_in_t serv_addr;
    int serv_port = PORT;
    int listen_fd;
    char tcp_buf[TCPBUF_LEN];
    int th_used = 0;
    int tn_fft = -1;
    int opt_fft = 0;
    int fft_num = 0;

    pcm_t pcm = {0};


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _fileno(stdin)
#endif
    //setbuf(FPOUT, NULL);

    sigaction(SIGPIPE, &(struct sigaction){SIG_IGN}, NULL);


    for (k = 0; k < MAX_FQ; k++) base_fqs[k] = 0.0;

    // server options
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if (strcmp(*argv, "--dbg") == 0) {
            option_dbg = 1;
        }
        else if (strcmp(*argv, "--enable_clsv_out") == 0) {
            option_clsv_out = 1;
        }
        else if (strcmp(*argv, "--port") == 0) {
            int port = 0;
            ++argv;
            if (*argv) port = atoi(*argv); else return -1;
            if (port < PORT_LO || port > PORT_HI) {
                fprintf(stderr, "error: port %d..%d\n", PORT_LO, PORT_HI);
            }
            else serv_port = port;
        }
        else if (strncmp(*argv, "--fft", 5) == 0) {
            char *arg_fft = *argv;
            if      (strncmp(arg_fft+5, "_avg", 4) == 0) opt_fft = OPT_FFT_SERV | OPT_FFT_AVG;
            else if (strncmp(arg_fft+5, "_all", 4) == 0) opt_fft = OPT_FFT_SERV;
            else return -1;
            if (xlt_cnt < MAX_FQ) {
                base_fqs[xlt_cnt] = 0.0;
                rstype[xlt_cnt] = thd_FFT;
                tn_fft = xlt_cnt;
                xlt_cnt++;
            }
            ++argv;
            if (*argv) fft_num = atoi(*argv); else return -1;
            ++argv;
            if (*argv) fname_fft = *argv; else return -1;
        }
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
        else if (strcmp(*argv, "--bo") == 0) {
            int bps_out = 0;
            ++argv;
            if (*argv) bps_out = atoi(*argv); else return -1;
            if ((bps_out != 8 && bps_out != 16 && bps_out != 32)) {
                bps_out = 0;
            }
            pcm.bps_out = bps_out;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error open %s\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

    pcm.fp = fp;
    if (option_pcmraw == 0) {
        k = read_wav_header( &pcm );
        if ( k < 0 ) {
            fclose(fp);
            fprintf(stderr, "error: wav header\n");
            return -1;
        }
    }
    if (pcm.nch < 2) {
        fprintf(stderr, "error: iq channels < 2\n");
        return -50;
    }

    pcm.opt_IFmin = option_min;
    pcm_dec_init( &pcm );

    block_decMB = calloc(pcm.decM*blk_sz+1, sizeof(float complex));  if (block_decMB == NULL) return -1;


    thargs_t tharg[MAX_FQ]; // xlt_cnt<=MAX_FQ
    for (k = 0; k < MAX_FQ; k++) tharg[k].thd.used = 0;
    for (k = 0; k < MAX_FQ; k++) tharg[k].thd.fft = 0;

    for (k = 0; k < xlt_cnt; k++) {
        if (k == tn_fft) {
            tharg[k].thd.fft = opt_fft;
            tharg[k].fname = fname_fft;
            tharg[k].thd.fft_num = fft_num;
            tharg[k].thd.stop_fft = 0;

            if ( (tharg[k].thd.fft & 0xF) == OPT_FFT_SERV ) { // save FFT at server
                if (fname_fft) {
                    if (fname_fft[0] == '-') tharg[k].fpo = stdout;
                    else tharg[k].fpo = fopen(fname_fft, "wb");
                }
                else return -1;
                if (tharg[k].fpo == NULL) {
                    fprintf(stderr, "error: open %s\n", fname_fft);
                }
            }
        }
        tharg[k].thd.tn = k;
        tharg[k].thd.tn_bit = (1<<k);
        tharg[k].thd.mutex = &mutex;
        tharg[k].thd.cond = &cond;
        //tharg[k].thd.lock = &lock;
        tharg[k].thd.blk = block_decMB;
        tharg[k].thd.max_fq = xlt_cnt;
        tharg[k].thd.xlt_fq = -base_fqs[k]; // S(t)*exp(-f*2pi*I*t): fq baseband -> IF (rotate from and decimate)

        tharg[k].pcm = pcm;

        rbf1 |= tharg[k].thd.tn_bit;
        tharg[k].thd.used = 1;
    }

    for (k = 0; k < xlt_cnt; k++) {
        pthread_create(&tharg[k].thd.tid, NULL, rstype[k], &tharg[k]);
    }


    // TCP server
    //
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htons(INADDR_ANY);
    serv_addr.sin_port = htons(serv_port);


    listen_fd = socket(AF_INET, SOCK_STREAM, 0); // TCP
    if (listen_fd < 0) {
        fprintf(stderr, "error: socket\n");
        return 1;
    }

    int sockopt = 1;
    if ( setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)) < 0 ) {
        fprintf(stderr, "error: reuse socket\n");
    }


    if ( bind(listen_fd, (sa_t *) &serv_addr, sizeof(serv_addr)) < 0 ) {
        fprintf(stderr, "error: bind\n");
        return 2;
    }

    if ( listen(listen_fd, SERV_BACKLOG) < 0 ) {
        fprintf(stderr, "error: listen\n");
        return 3;
    }


    while ( 1 ) {

        int l = 0;

        fprintf(stderr, "waiting on port %d\n", serv_port);

        int conn_fd = accept(listen_fd, (sa_t *)NULL, NULL);
        if (conn_fd < 0) {
            fprintf(stderr, "error: connect\n");
            //
            return 4;
        }

        if (option_dbg) {
            fprintf(stderr, "conn_fd: %d\n", conn_fd);
        }

        memset(tcp_buf, 0, TCPBUF_LEN);

        l = read(conn_fd, tcp_buf, TCPBUF_LEN-1);
        if (l < 0) {
            fprintf(stderr, "error: read socket\n");
            return 5;
        }

        if (option_dbg) {
            fprintf(FPOUT, "\"%s\"\n", tcp_buf);
        }


        th_used = 0;
        for (k = 0; k < MAX_FQ; k++) th_used += tharg[k].thd.used;
        //if (th_used == 0) break;


        // client commands
        if ( l > 1 ) {
            char *freq = tcp_buf;
            while (l > 1 && tcp_buf[l-1] < 0x20) l--;
            tcp_buf[l] = '\0'; // remove \n, terminate string

            if (strncmp(tcp_buf, "--freq ", 7) == 0) {
                freq = tcp_buf + 7;
            }
            else {
                if ( strcmp(tcp_buf, "--stop") == 0 ) {
                    close(conn_fd);
                    for (k = 0; k < MAX_FQ; k++) tharg[k].thd.used = 0;
                    break;
                }
                else if ( strncmp(tcp_buf, "--fft", 5) == 0 ) {
                    opt_fft = 0;
                    if      (strncmp(tcp_buf+5, "_avg", 4) == 0) opt_fft = OPT_FFT_AVG;
                    else if (strncmp(tcp_buf+5, "_all", 4) != 0) return -1;
                    if      (strncmp(tcp_buf+5+4, "_cl", 3) == 0) opt_fft |= OPT_FFT_CLNT;
                    else if (strncmp(tcp_buf+5+4, "_sv", 3) == 0) {
                        if (option_clsv_out) opt_fft |= OPT_FFT_SERV;
                        else {
                            pthread_mutex_lock( &mutex );
                            fprintf(FPOUT, "<NO CLSV OUT>\n");
                            pthread_mutex_unlock( &mutex );
                            close(conn_fd);
                            continue;
                        }
                    }
                    else return -1;
                    if (tcp_buf+5+4+4) fft_num = atoi(tcp_buf+5+4+4); else fft_num = 0;
                    char *fname_fft_cl = "db_fft_cl.txt";
                    char *pbuf = tcp_buf;
                    for (pbuf = tcp_buf+5+4+4; *pbuf; pbuf++) {
                        if (*pbuf == ' ') break;
                    }
                    if (*pbuf == ' ') {
                        fname_fft_cl = pbuf+1;
                    }
                    if ( !tcp_eof )
                    {
                        if (tn_fft >= 0) {
                            if (tharg[tn_fft].thd.fft == 0) {
                                tharg[tn_fft].thd.fft_num = fft_num;
                                tharg[tn_fft].thd.stop_fft = 0;
                                tharg[tn_fft].fname = fname_fft_cl;
                                tharg[tn_fft].fd = conn_fd;

                                if ( (opt_fft & 0xF) == OPT_FFT_SERV ) { // save FFT at server
                                    if (fname_fft_cl) {
                                        if (fname_fft_cl[0] == '-') tharg[tn_fft].fpo = stdout;
                                        else tharg[tn_fft].fpo = fopen(fname_fft_cl, "wb");
                                    }
                                    else return -1;
                                    if (tharg[tn_fft].fpo == NULL) {
                                        fprintf(stderr, "error: open %s\n", fname_fft_cl);
                                    }
                                    close(tharg[tn_fft].fd); tharg[tn_fft].fd = -1; // tharg[tn_fft].fd == conn_fd
                                }
                                else { // (opt_fft & 0xF) == OPT_FFT_CLNT : send FFT to client
                                    tharg[tn_fft].fpo = NULL;
                                }

                                tharg[tn_fft].thd.fft = opt_fft;
                            }
                            else {
                                pthread_mutex_lock( &mutex );
                                fprintf(FPOUT, "<%d: FFT running>\n", tn_fft);
                                pthread_mutex_unlock( &mutex );

                                close(conn_fd);
                            }
                        }
                        else {
                            for (k = 0; k < MAX_FQ; k++) {
                                if (tharg[k].thd.used == 0) {
                                    if (k != tn_fft) break;
                                }
                            }
                            if (k < MAX_FQ) {
                                tharg[k].thd.fft_num = fft_num;
                                tharg[k].thd.stop_fft = 0;
                                tharg[k].fname = fname_fft_cl;
                                tharg[k].fd = conn_fd;

                                tn_fft = k;
                                tharg[k].thd.tn = k;
                                tharg[k].thd.tn_bit = (1<<k);
                                tharg[k].thd.mutex = &mutex;
                                tharg[k].thd.cond = &cond;
                                //tharg[k].thd.lock = &lock;
                                tharg[k].thd.blk = block_decMB;
                                tharg[k].thd.xlt_fq = 0.0;
                                tharg[k].pcm = pcm;

                                if ( (opt_fft & 0xF) == OPT_FFT_SERV ) { // save FFT at server
                                    if (fname_fft_cl) {
                                        if (fname_fft_cl[0] == '-') tharg[k].fpo = stdout;
                                        else tharg[k].fpo = fopen(fname_fft_cl, "wb");
                                    }
                                    else return -1;
                                    if (tharg[k].fpo == NULL) {
                                        fprintf(stderr, "error: open %s\n", fname_fft_cl);
                                    }
                                    close(tharg[k].fd); tharg[k].fd = -1; // tharg[k].fd == conn_fd
                                }
                                else { // (opt_fft & 0xF) == OPT_FFT_CLNT : send FFT to client
                                    tharg[k].fpo = NULL;
                                }

                                rbf1 |= tharg[k].thd.tn_bit;
                                tharg[k].thd.used = 1;
                                tharg[k].thd.fft = opt_fft;

                                pthread_create(&tharg[k].thd.tid, NULL, thd_FFT, &tharg[k]);

                                k++;
                                if (k > xlt_cnt) xlt_cnt = k;
                                for (k = 0; k < xlt_cnt; k++) {
                                    tharg[k].thd.max_fq = xlt_cnt;
                                }
                            }
                            else {
                                close(conn_fd);
                            }
                        }
                    }
                    else {
                        close(conn_fd);
                    }
                }
                else if (tcp_buf[0] == '-') { // -<n> : close <n>
                    int num = atoi(tcp_buf+1);
                    if (num >= 0 && num < MAX_FQ) {
                        if (num != tn_fft)
                        {
                            tharg[num].thd.used = 0;
                        }
                        else
                        {
                            tharg[num].thd.stop_fft = 1;
                        }
                    }
                    close(conn_fd);
                }
                continue;
            }

            double fq = 0.0;
            if (freq) fq = atof(freq);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;

            // find slot
            for (k = 0; k < MAX_FQ; k++) {
                if (tharg[k].thd.used == 0) break;
            }
            if (k < MAX_FQ) {
                double base_fq = fq;

                tharg[k].thd.tn = k;
                tharg[k].thd.tn_bit = (1<<k);
                tharg[k].thd.mutex = &mutex;
                tharg[k].thd.cond = &cond;
                //tharg[k].thd.lock = &lock;
                tharg[k].thd.blk = block_decMB;
                tharg[k].thd.xlt_fq = -base_fq;

                tharg[k].pcm = pcm;
                tharg[k].fd = conn_fd;

                rbf1 |= tharg[k].thd.tn_bit;
                tharg[k].thd.used = 1;
                tharg[k].thd.fft = 0;
                tharg[k].thd.stop_fft = 0;

                pthread_create(&tharg[k].thd.tid, NULL, thd_IF, &tharg[k]);

                pthread_mutex_lock( &mutex );
                fprintf(FPOUT, "<%d: ADD f=%+.4f>\n", k, base_fq);
                pthread_mutex_unlock( &mutex );

                k++;
                if (k > xlt_cnt) xlt_cnt = k;
                for (k = 0; k < xlt_cnt; k++) {
                    tharg[k].thd.max_fq = xlt_cnt;
                }

            }
        }
    }

    for (k = 0; k < xlt_cnt; k++) {
        pthread_join(tharg[k].thd.tid, NULL);
    }

    th_used = 1;
    while (th_used) {
        th_used = 0;
        for (k = 0; k < MAX_FQ; k++) th_used += tharg[k].thd.used;
    }

    if (block_decMB) { free(block_decMB); block_decMB = NULL; }
    decimate_free();

    fclose(fp);

    close(listen_fd);

    return 0;
}

