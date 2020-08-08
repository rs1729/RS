
/*
 *  compile:
 *
 *      gcc -O2 iq_client.c -o iq_client
 *
 *  author: zilog80
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>

#include "iq_svcl.h"


static sa_in_t serv_addr;
static int serv_port = PORT;
static int sock_fd;
static char *str_addr = "127.0.0.1"; // 0.0.0.0

static int send_len, recv_len;
static char sendln[LINELEN+1];
static char recvln[LINELEN+1];


int main(int argc, char *argv[]) {

    int re = 0;
    char *fname_fft = "db_fft_cl.txt";

    sock_fd = socket(AF_INET, SOCK_STREAM, 0); // TCP
    if (sock_fd < 0) {
        fprintf(stderr, "error: create socket\n");
        return 1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(serv_port);

    memset(sendln, 0, LINELEN+1);

    ++argv;
    while ( *argv ) {
        if (strcmp(*argv, "--ip") == 0) {
            ++argv;
            if (*argv) str_addr = *argv; else return -1;
        }
        else if (strcmp(*argv, "--fft0") == 0) {
            sprintf(sendln, "%s", "--fft0");
        }
        else if (strcmp(*argv, "--fft") == 0) {
            sprintf(sendln, "%s", "--fft");
            ++argv;
            if (*argv) {
                fname_fft = *argv;
            }
            else return -1;
            re = 2;
        }
        else if (strcmp(*argv, "--freq") == 0) {
            ++argv;
            if (*argv) {
                snprintf(sendln, LINELEN, "--freq %s", *argv);
                re = 1;
            }
            else return -1;
        }
        else if ((*argv)[0] == '-' && (*argv)[1] != '-') {
            snprintf(sendln, LINELEN,"%s", *argv);
        }
        else if (strcmp(*argv, "--stop") == 0) {
            sprintf(sendln, "%s", "--stop");
        }
        ++argv;
    }

    if ( *sendln )
    {
        if (inet_pton(AF_INET, str_addr, &serv_addr.sin_addr) <= 0) {
            fprintf(stderr, "error: inet_pton %s\n", str_addr);
            return 2;
        }

        if ( connect(sock_fd, (sa_t *) &serv_addr, sizeof(serv_addr)) < 0 ) {
            fprintf(stderr, "error: connect\n");
            return 3;
        }

        send_len = strlen(sendln);

        if ( write(sock_fd, sendln, send_len) != send_len ) {
            fprintf(stderr, "error: write socket\n");
            return 4;
        }

        if ( re )
        {
            //int count = 0;
            int len;

            if ( re == 1 )
            {
                // header
                memset(recvln, 0, LINELEN+1);
                len = HDRLEN;
                while ( (recv_len = read(sock_fd, recvln, len)) > 0) {
                    recvln[recv_len] = '\0';
                    if ( *recvln ) fprintf(stderr, "%s", recvln);
                    len -= recv_len;
                    if (len == 0) break;
                }
                if (recv_len < 0) {
                    fprintf(stderr, "error: read socket\n");
                    return 5;
                }


                //
                // data

                memset(recvln, 0, LINELEN+1);

                //ioctl(sock_fd, FIONREAD, &count);
                while ( (recv_len = read(sock_fd, recvln, LINELEN)) > 0) {

                    len = fwrite(recvln, recv_len, 1, stdout);
                    if (len != 1) {
                        fprintf(stderr, "error: write  %d blocks\n", len);
                        break;
                    }
                    memset(recvln, 0, LINELEN+1);
                    //ioctl(sock_fd, FIONREAD, &count);
                }
                if (recv_len < 0) {
                    fprintf(stderr, "error: read socket\n");
                    //return 5;
                }
            }
            else if ( re == 2 )
            {
                // fft data
                FILE *fpo = fopen(fname_fft, "wb");
                if (fpo != NULL) {

                    memset(recvln, 0, LINELEN+1);

                    //ioctl(sock_fd, FIONREAD, &count);
                    while ( (recv_len = read(sock_fd, recvln, LINELEN)) > 0) {

                        len = fwrite(recvln, recv_len, 1, fpo);
                        if (len != 1) {
                            fprintf(stderr, "error: write  %d blocks\n", len);
                            break;
                        }
                        memset(recvln, 0, LINELEN+1);
                        //ioctl(sock_fd, FIONREAD, &count);
                    }
                    if (recv_len < 0) {
                        fprintf(stderr, "error: read socket\n");
                        //return 5;
                    }

                    fclose(fpo);
                }
                else {
                    fprintf(stderr, "error: open %s\n", fname_fft);
                }

            }
        }

        if ( sock_fd > 0 ) {
            close(sock_fd);
        }
    }

    return 0;
}

