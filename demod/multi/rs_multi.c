
/*

gcc -O2 -c demod_base.c
gcc -O2 -c bch_ecc_mod.c
gcc -O2 -c rs41base.c
gcc -O2 -c dfm09base.c
gcc -O2 -c m10base.c
gcc -O2 -c m20base.c
gcc -O2 -c lms6Xbase.c
gcc -O2 rs_multi.c demod_base.o bch_ecc_mod.o rs41base.o dfm09base.o m10base.o m20base.o lms6Xbase.o -lm -pthread

./a.out --rs41 <fq0> --dfm <fq1> --m10 <fq2> baseband_IQ.wav
-0.5 < fq < 0.5 , fq=freq/sr

e.g.
[terminal 1]
./a.out --dc --fifo rsfifo --m10 -0.28034 --lms 0.12636  baseband_IQ.wav
[terminal 2]
echo "m10 -0.36" > rsfifo
echo "-1" > rsfifo
echo "lms 0.02428" > rsfifo

*/


#include <stdio.h>

#include <sys/stat.h> // mkfifo()
#include <unistd.h> // open(),close(),unlink()
#include <fcntl.h> // O_RDONLY //... setmode()/cygwin

#ifdef CYGWIN
  //#include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif

#include "demod_base.h"


static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
//static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

static float complex *block_decMB;

extern int rbf1;   // demod_base.c
extern int bufeof; // demod_base.c

void *thd_rs41(void *);
void *thd_dfm09(void *);
void *thd_m10(void *);
void *thd_m20(void *);
void *thd_lms6X(void *);


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

    iq_dc_init(p);

    fprintf(stderr, "IF: %d\n", IF_sr);
    fprintf(stderr, "dec: %d\n", decM);

    fprintf(stderr, "taps: %d\n", taps);
    fprintf(stderr, "transBW: %.4f = %.1f Hz\n", tbw/sr_base, tbw);
    fprintf(stderr, "f: +/-%.4f = +/-%.1f Hz\n", f_lp, f_lp*sr_base);

    return 0;
}

#define FIFOBUF_LEN 20

int main(int argc, char **argv) {

    FILE *fp;
    int wavloaded = 0;
    int k;
    int xlt_cnt = 0;
    int cfreq = -1;
    double base_fqs[MAX_FQ];
    void *rstype[MAX_FQ];
    int option_pcmraw = 0,
        option_jsn = 0,
        option_dc  = 0,
        option_min = 0,
        option_cont = 0;

    // FIFO
    int  option_fifo = 0;
    char *rs_fifo = NULL;
    int  fd = -1;
    char fifo_buf[FIFOBUF_LEN];
    int th_used = 0;

#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _fileno(stdin)
#endif
    setbuf(stdout, NULL);


    pcm_t pcm = {0};

    for (k = 0; k < MAX_FQ; k++) base_fqs[k] = 0.0;

    ++argv;
    while ((*argv) && (!wavloaded)) {
        if (strcmp(*argv, "--rs41") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv); else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            if (xlt_cnt < MAX_FQ) {
                base_fqs[xlt_cnt] = fq;
                rstype[xlt_cnt] = thd_rs41;
                xlt_cnt++;
            }
        }
        else if (strcmp(*argv, "--dfm") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv); else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            if (xlt_cnt < MAX_FQ) {
                base_fqs[xlt_cnt] = fq;
                rstype[xlt_cnt] = thd_dfm09;
                xlt_cnt++;
            }
        }
        else if (strcmp(*argv, "--m10") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv); else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            if (xlt_cnt < MAX_FQ) {
                base_fqs[xlt_cnt] = fq;
                rstype[xlt_cnt] = thd_m10;
                xlt_cnt++;
            }
        }
        else if (strcmp(*argv, "--m20") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv); else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            if (xlt_cnt < MAX_FQ) {
                base_fqs[xlt_cnt] = fq;
                rstype[xlt_cnt] = thd_m20;
                xlt_cnt++;
            }
        }
        else if (strcmp(*argv, "--lms") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv); else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            if (xlt_cnt < MAX_FQ) {
                base_fqs[xlt_cnt] = fq;
                rstype[xlt_cnt] = thd_lms6X;
                xlt_cnt++;
            }
        }
        else if   (strcmp(*argv, "--json") == 0) {
            option_jsn = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;  // center frequency / Hz
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 350000000 || frq > 450000000) frq = -1;
            cfreq = frq;
        }
        else if   (strcmp(*argv, "--dc") == 0) {
            option_dc = 1;
        }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--cnt") == 0) ) {
            option_cont = 1;
        }
        else if   (strcmp(*argv, "--fifo") == 0) {
            ++argv;
            if (*argv) rs_fifo = *argv; else return -1;
            unlink(rs_fifo);
            if (mkfifo(rs_fifo, 0666) < 0) { // evtl. mode : S_IWUSR | S_IRUSR
                //fprintf(stderr, "error open %s\n", rs_fifo); // exit, if exists ?
            }
            option_fifo = 1;
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

    for (k = 0; k < xlt_cnt; k++) {
        tharg[k].thd.tn = k;
        tharg[k].thd.tn_bit = (1<<k);
        tharg[k].thd.mutex = &mutex;
        tharg[k].thd.cond = &cond;
        //tharg[k].thd.lock = &lock;
        tharg[k].thd.blk = block_decMB;
        tharg[k].thd.max_fq = xlt_cnt;
        tharg[k].thd.xlt_fq = -base_fqs[k]; // S(t)*exp(-f*2pi*I*t): fq baseband -> IF (rotate from and decimate)
        if (cfreq > 0) {
            int fq_kHz = (cfreq - tharg[k].thd.xlt_fq*pcm.sr_base + 500)/1e3;
            tharg[k].jsn_freq = fq_kHz;
        }

        tharg[k].pcm = pcm;

        tharg[k].option_jsn = option_jsn;
        tharg[k].option_dc  = option_dc;
        tharg[k].option_cnt = option_cont;

        rbf1 |= tharg[k].thd.tn_bit;
        tharg[k].thd.used = 1;
    }

    for (k = 0; k < xlt_cnt; k++) {
        pthread_create(&tharg[k].thd.tid, NULL, rstype[k], &tharg[k]);
    }


    // FIFO
    //
    if (option_fifo)
    {
        fd = open(rs_fifo, O_RDONLY | O_NONBLOCK); //fcntl.h
        if (fd < 0) {
            fprintf(stderr, "error open %s\n", rs_fifo);
            return -1;
        }

        while ( !bufeof ) {
            int l = 0;
            memset(fifo_buf, 0, FIFOBUF_LEN);

            th_used = 0;
            for (k = 0; k < MAX_FQ; k++) th_used += tharg[k].thd.used;
            if (th_used == 0) break;

            l = read(fd, fifo_buf, FIFOBUF_LEN);
            if ( l > 1 ) {
                void *rstype = NULL;
                char *fifo_fq = fifo_buf;
                while (l > 1 && fifo_buf[l-1] < 0x20) l--;
                fifo_buf[l] = '\0'; // remove \n, terminate string
                if (strncmp(fifo_buf, "rs41", 4) == 0) {
                    fifo_fq = fifo_buf + 4;
                    rstype = thd_rs41;
                }
                else if (strncmp(fifo_buf, "dfm", 3) == 0) {
                    fifo_fq = fifo_buf + 3;
                    rstype = thd_dfm09;
                }
                else if (strncmp(fifo_buf, "m10", 3) == 0) {
                    fifo_fq = fifo_buf + 3;
                    rstype = thd_m10;
                }
                else if (strncmp(fifo_buf, "m20", 3) == 0) {
                    fifo_fq = fifo_buf + 3;
                    rstype = thd_m20;
                }
                else if (strncmp(fifo_buf, "lms", 3) == 0) {
                    fifo_fq = fifo_buf + 3;
                    rstype = thd_lms6X;
                }
                else {
                    if (fifo_buf[0] == '-') { // -<n> : close <n>
                        int num = atoi(fifo_buf+1);
                        if (num >= 0 && num < MAX_FQ) {
                            if (tharg[num].thd.used) {
                                tharg[num].thd.used = 0;
                            }
                        }
                    }
                    continue;
                }

                double fq = 0.0;
                if (fifo_fq) fq = atof(fifo_fq);
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
                    if (cfreq > 0) {
                        int fq_kHz = (cfreq - tharg[k].thd.xlt_fq*pcm.sr_base + 500)/1e3;
                        tharg[k].jsn_freq = fq_kHz;
                    }

                    tharg[k].pcm = pcm;

                    tharg[k].option_jsn = option_jsn;
                    tharg[k].option_dc  = option_dc;

                    rbf1 |= tharg[k].thd.tn_bit;
                    tharg[k].thd.used = 1;

                    pthread_create(&tharg[k].thd.tid, NULL, rstype, &tharg[k]);

                    pthread_mutex_lock( &mutex );
                    fprintf(stdout, "<%d: add f=%+.4f>\n", k, base_fq);
                    pthread_mutex_unlock( &mutex );

                    k++;
                    if (k > xlt_cnt) xlt_cnt = k;
                    for (k = 0; k < xlt_cnt; k++) {
                        tharg[k].thd.max_fq = xlt_cnt;
                    }

                }
            }
            sleep(1);
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

    if (fd >= 0) { // FIFO
        close(fd);
        unlink(rs_fifo);
    }

    return 0;
}

