

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>


#define TCPBUF_LEN 1024
#define SERV_BACKLOG 6

#define HDRLEN 256

#define PORT 1280

typedef struct sockaddr    sa_t;
typedef struct sockaddr_in sa_in_t;


