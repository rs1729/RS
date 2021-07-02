
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef short i16_t;
typedef int   i32_t;

#define LINELEN 1024

static int option_verbose = 1;

static float *db, *intdb;
static float *fq; //, *freq;
static float *peak;


int main(int argc, char **argv) {

    FILE *OUT = stderr;
    FILE *fpout = stdout;
    FILE *fp = NULL;


    int j, n, k;
    int N;
    int f0 = 0;
    int sr = 0;

    int dn;
    float dx;
    float sympeak = 0.0;
    float sympeak2 = 0.0;
    float globmin = 0.0;
    float globavg = 0.0;

    char line[LINELEN+1];
    char *pbuf = NULL;
    char *p1, *p2;
    int c;

#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    if (argv[1] == NULL) {
        fprintf(stderr, "usage:\n");
        fprintf(stderr, "\t%s <fft_avg.csv>\n", argv[0]);
        return 1;
    }
    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        fprintf(stderr, "error: open %s\n", argv[1]);
        return 1;
    }


    memset(line, 0, LINELEN+1);

    N = 0;
    j = 0;
    n = 0;
    // sec.ms,freq_min,freq_max,Hz/bin,N_bins, ...
    while ( (c = fgetc(fp)) != EOF) {
        if (c == '\n') break;
        if (c == ' ') continue;
        if (c == ',') {
            if (n == 1) {
                int freq_min = atoi(line);
                sr = -2*freq_min;
            }
            if (n == 4) {
                N = atoi(line);
                break;
            }

            n++;
            memset(line, 0, LINELEN+1);
            j = 0;
        }
        else {
            line[j] = c;
            j++;
        }
    }

    db = calloc(N+1, sizeof(float));  if (db == NULL) return 2;
    fq = calloc(N+1, sizeof(float));  if (fq == NULL) return 2;
    //freq = calloc(N+1, sizeof(float));  if (freq == NULL) return 2;

    intdb = calloc(N+1, sizeof(float));  if (intdb == NULL) return 2;
    peak = calloc(N+1, sizeof(float));  if (peak == NULL) return 2;


    // ..., db_1,...,db_N:
    memset(line, 0, LINELEN+1);
    j = 0;
    n = 0;
    while ( (c = fgetc(fp)) != EOF) {
        if (c == '\n') break;
        if (c == ' ') continue;
        if (c == ',') {
            if (n < N) {
                db[n] = atof(line);
                fq[n] = -0.5 + n/(float)N;
            }

            n++;
            memset(line, 0, LINELEN+1);
            j = 0;
        }
        else {
            line[j] = c;
            j++;
        }
    }

    f0 = N/2;

    globmin = 0.0;
    globavg = 0.0;
    float db_spike3 = 10.0;
    int spike_wl3 = 3; //freq2bin(&DFT, 200); // 3 // 200 Hz
    int spike_wl5 = 5; //freq2bin(&DFT, 200); // 3 // 200 Hz


    dx = 200.0;
    if (sr) dx = sr*(fq[f0+1]-fq[f0]); //freq[f0+1]-freq[f0];
    dn = 2*(int)(2400.0/dx)+1; // (odd/symmetric) integration width: 4800+dx Hz
    if (option_verbose > 1) fprintf(stderr, "dn = %d\n", dn);

    // dc-spike (N-1,)N,0,1(,2): subtract mean/avg
    // spikes in general:
    for (j = 0; j < N; j++) {
        if ( db[j] - db[(j-spike_wl5+N)%N] > db_spike3
          && db[j] - db[(j-spike_wl3+N)%N] > db_spike3
          && db[j] - db[(j+spike_wl3+N)%N] > db_spike3
          && db[j] - db[(j+spike_wl5+N)%N] > db_spike3
           ) {
            db[j] = (db[(j-spike_wl3+N)%N]+db[(j+spike_wl3+N)%N])/2.0;
        }
    }

    for (j = 0; j < N; j++) {
        float sum = 0.0;
        for (n = j-(dn-1)/2; n <= j+(dn-1)/2; n++) sum += db[(n + N) % N];
        sum /= (float)dn;
        intdb[j] = sum;
        globavg += sum; // <=> db[j];
        if (sum < globmin) globmin = sum;
    }
    globavg /= (float)N;

    if (option_verbose) fprintf(stderr, "avg=%.2f\n", globavg);
    if (option_verbose) fprintf(stderr, "min=%.2f\n", globmin);

    int dn2 = 2*dn+1;
    int dn3 = (int)(4000.0/dx); // (odd/symmetric) integration width: +/-4000 Hz

    int delay = (int)(24000.0/dx); // 16000
    float *bufpeak = calloc(delay+1, sizeof(float)); if (bufpeak == NULL) return -1;
    int mag = 0;
    int mag0 = 0;
    float max_db_loc = 0.0;
    int   max_db_idx = 0;
    float max_mag = 0.0;

    k = 0;

    for (j = 0; j < N; j++) {  // j = N/2; j < N/2 + N; j % N

        sympeak = 0.0;
        for (n = 1; n <= dn3; n++) {
            sympeak += (db[(j+n) % N]-globmin)*(db[(j-n + N) % N]-globmin);
        }
        sympeak = sqrt(abs(sympeak)/(float)dn3);  // globmin > min

        sympeak2 = 0.0;
        for (n = 0; n <= (dn2-1)/2; n++) sympeak2 += (intdb[(j+n) % N]-globmin)*(intdb[(j-n + N) % N]-globmin);
        sympeak2 = sqrt(sympeak2/(2.0*dn2));

        peak[k % delay] = sympeak;

        mag = (sympeak - peak[(k+1)%delay])/ 3.0;  // threshold 3.0
        if ( mag < 0 ) mag = 0;

        #ifdef DBG
        fprintf(fpout, "%9.6f; %10.4f; %10.4f; %10.4f; %d\n", fq[j % N], intdb[j % N], sympeak, sympeak2, mag);
        #endif

        if (mag0 > 0 && mag == 0) {
            if ( fabs(fq[max_db_idx]) < 0.425 ) // 85% bandwidth
            {
                fprintf(OUT, "peak: %+11.8f = %+8.0f Hz", fq[max_db_idx], sr*fq[max_db_idx]);
                fprintf(OUT, "  (%.1fdB)", max_db_loc);
                //fprintf(OUT, "  (%.1f)", max_mag);
                fprintf(OUT, "\n");
            }
        }
        if (mag0 == 0 && mag > 0) {
            max_db_loc = sympeak;
            max_db_idx = j % N;
        }
        if (mag > 0 && sympeak > max_db_loc) {
            max_db_loc = sympeak;
            max_db_idx = j % N;
            max_mag = (sympeak - peak[(k+1)%delay]);
        }

        mag0 = mag;

        k++;
    }

    if (option_verbose) fprintf(stderr, "bin = %.0f Hz\n", sr*(fq[f0+1]-fq[f0])); //freq[f0+1]-freq[f0]

    if (bufpeak) free(bufpeak);

    fclose(fp);


    return 0;
}

