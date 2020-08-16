

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>


#define TCPBUF_LEN 1024
#define SERV_BACKLOG 6

#define LINELEN 4096
#define HDRLEN  256

#define PORT    1280
#define PORT_LO 1024
#define PORT_HI 65353

typedef struct sockaddr    sa_t;
typedef struct sockaddr_in sa_in_t;


