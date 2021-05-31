#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <netdb.h>
#include <net/if_tun.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <event.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h> 
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>




__dead void usage(void);

struct echod {
    TAILQ_ENTRY(echod) 
                    entry;
    int tapFd;
    int destFd;
    uint32_t vni;
    struct timeval timeout;
    struct event *timeoutEvent;
    struct event ev;
};
TAILQ_HEAD(echod_list, echod);
/*
 *It contains configuration information
 */
struct ipConfig{
    const char* destPort;
    const char* srcPort;
    int ipVersion;
    int timeout;
    const char* address;
    char** tunnels;
};

/*
 *Usage: called when user has not met the requirements to run the program
 */

__dead void 
usage(void) 
{
    extern char *__progname;
    fprintf(stderr, "usage: %s [-46d] [-l address] [-p port]" 
    " -t 120\n             -e /dev/tapX@vni\n"
    "             server [port]\n",__progname);
    exit(1);
}

/*
 * creates and binds a socket to localaddress(if given) and will also connect
 * to server. and returns file descriptor 
 */
int 
connect_to_host(char* hostname, struct ipConfig* newIp)
{
    int error;
    struct addrinfo *addressInfo, *rp, *localInfo;
    struct addrinfo hints;
    struct addrinfo localBind;
    int fd;
    memset(&localBind, 0, sizeof(struct addrinfo));
    localBind.ai_family = newIp->ipVersion;
    localBind.ai_socktype = SOCK_DGRAM;
    localBind.ai_flags = AI_PASSIVE;
    localBind.ai_protocol = IPPROTO_UDP;
    error = getaddrinfo(newIp->address, newIp->srcPort, &localBind, &localInfo);
    if(error)
	errx(1,"%s",gai_strerror(error));
        
    for (rp = localInfo; rp!= NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(fd == -1)
            continue;
        
        if (bind(fd, localInfo->ai_addr, (int)localInfo->ai_addrlen ) == -1) {
            close(fd);
            fd = -1;
            continue;
        }
        
        break;
    }
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = newIp->ipVersion;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_UDP;
    error = getaddrinfo(hostname, newIp->destPort, &hints, &addressInfo);
    if(error)
	errx(1, "%s", gai_strerror(error));
        

    for (rp = addressInfo; rp!= NULL; rp = rp->ai_next) {
                
        if(connect(fd, rp->ai_addr, rp->ai_addrlen) == -1) {
            if (rp->ai_next == NULL) 
                fd = -1;
            else
                continue;
        }
        break;

    }
    if (fd == -1)
        err(1, "%s", strerror(errno));

    freeaddrinfo(addressInfo);
    freeaddrinfo(localInfo);
    return fd;

}

/*
 *Time out function to close file descriptors and exit program with code 1.
 */
void
timeout(int fd, short revents, void *conn)
{
    struct echod_list *echods = (struct echod_list*)conn;
    struct echod *e;
    TAILQ_FOREACH(e, echods, entry) {
        close(e->destFd);
        close(e->tapFd);   
    }
    exit(1);


}

/*
 * Reads from server socket and decapsulates message and sends it to 
 * tap device.
 */
void
communicate_server(int fd, short revents, void *conn)
{
    static uint8_t buffer[65536];
    struct echod_list *echods = (struct echod_list*)conn;
    struct echod *ec = TAILQ_FIRST(echods);
    if (ec->timeout.tv_sec > 0) {
        evtimer_add(ec->timeoutEvent, &(ec->timeout));
    }
    size_t nread;
    nread = read(fd, buffer, sizeof(buffer));
    if (nread == -1) {
        switch(errno) {
        case EINTR:
                break;
        default: 
                warn("Socket read error");
        }
        return;
    } else if (nread == 0) {
        errx(1, "Socket closed");
    }
    uint8_t version;
    uint16_t protocol;
    version = buffer[0]>>6;
    if (version != 0) 
        return;
    protocol = buffer[2] << 8 | buffer[3];
    if (protocol != 0x6558)
        return;
    uint32_t vni;
    vni = (buffer[4] << 16 | buffer[5] << 8 | buffer[6]);    
    char message[nread-8];
    for (int i = 0; i < nread; i++) {
        message[i] = buffer[i+8];
    }
    /* checks VNI and sends it through */
    struct echod *e;
    TAILQ_FOREACH(e, echods, entry) {
        if (e->vni == vni) {
            write(e->tapFd, message, nread);
        }   
    }
     
}

/*
 *Reads tap device and filters if neccasry and send to server socket.
 */

void
read_tap(int fd, short revents, void *conn) 
{
    struct echod *echod = (struct echod*)conn;
    if (echod->timeout.tv_sec > 0) {
        evtimer_add(echod->timeoutEvent, &(echod->timeout));
    }
    static uint8_t buffer[65536];
    uint16_t version = 0;
    size_t nread;
    nread = read(fd, buffer+8, sizeof(buffer)+8);
    if (nread == -1) {
        switch(errno) {
        case EINTR:
                break;
        default: 
                warn("Tap device read error");
        }
        return;
    } else if (nread == 0) {
        errx(1, "Tap device closed");
    }
    uint8_t ipVersion = buffer[22] >> 4;
    uint16_t protocol = htons(0x6558);
    uint32_t vni = 0;
    vni = htons((echod->vni) ) << 8;
    memcpy(buffer, &version,2);
    memcpy(buffer+2, &protocol, 2);
    memcpy(buffer+4, &vni, 4);
    
       /* Filer packets */
    uint32_t vnip = 0;
    vnip = (buffer[4] << 16 | buffer[5] << 8 | buffer[6]);    
    if ((echod->vni == 4096) && (ipVersion == 4))
        write(echod->destFd, buffer, nread+8);
    else if ((echod->vni == 8192) && (ipVersion == 6)) 
        write(echod->destFd, buffer, nread+8);
    else if ((echod->vni != 4096) && (echod->vni != 8192))
        write(echod->destFd, buffer, nread+8);
    
}
/*
 *connects to /dev/tapX@VNI ans inserts it into queue
 */
void 
connect_to_tap(struct echod_list *echods, struct ipConfig* newIp, int count, int destFd) 
{   

    const char* newErr;
    for (int i = 0; i < count; i++) {
        struct timeval tv;
        tv.tv_sec = newIp->timeout;
        tv.tv_usec = 0;
        char *tapFile = strtok(newIp->tunnels[i], "@");
        char *vni = strtok(NULL, "");
        int fd, err;
        struct ifreq ifr;
        if ((fd = open(tapFile, O_RDWR)) < 0) {
            errx(1, "Couldn't open device %s Error: %s", tapFile, strerror(errno));
            return;
        }
        memset(&ifr, 0, sizeof(ifr));
        
        if (((err = ioctl(fd, FIONBIO, &ifr)) < 0)) {
            perror("Error: ");
            close(fd);
            return;
        }
        /* add relavant information in echod and add it to queue */
        struct echod *newEchod = malloc(sizeof(struct echod));
        newEchod->tapFd = fd;
        newEchod->vni = strtonum(vni, 0, 1000000,&newErr);
            if (newErr) {
                errx(1, "vni  %s: %s \n", newErr, vni);
            } 
        newEchod->destFd = destFd;
        newEchod->timeout = tv;
        TAILQ_INSERT_TAIL(echods, newEchod, entry);
    }
}
/*
 * Sets up events
 */
void 
event_setup(int destFd,struct ipConfig *newIp , struct echod_list *echods) 
{
    struct echod *e;
    struct event *sockEvent = malloc(sizeof(struct event));
    struct event *timeEvent = malloc(sizeof(struct event));
    struct timeval tv;
    /* setup timer */ 
    tv.tv_sec = newIp->timeout;
    tv.tv_usec = 0;
    if (newIp->timeout > 0) {
        evtimer_set(timeEvent, timeout, echods);
        evtimer_add(timeEvent, &tv);
    }
    /* setup tap device */
    TAILQ_FOREACH(e, echods, entry) {
        event_set(&e->ev, e->tapFd, EV_READ|EV_PERSIST, read_tap, e); 
        event_add(&e->ev, NULL);
        e->timeoutEvent = timeEvent;
    }
    /* setup UDP */
    event_set(sockEvent, destFd, EV_READ|EV_PERSIST, communicate_server, echods); 
    event_add(sockEvent, NULL);
    
  }

int 
main(int argc, char* argv[]) 
{
    struct ipConfig newIp;
    newIp.tunnels = malloc(sizeof(char*) * 1000);    
    newIp.destPort = "6081";
    newIp.srcPort = NULL;
    newIp.ipVersion = AF_UNSPEC;
    newIp.timeout = -5;
    newIp.address = NULL;
    struct echod_list echods = TAILQ_HEAD_INITIALIZER(echods);
    const char *newErr;
    char* hostname;
    int destFd;
    int count = 0;
    bool deamon = true;
    int opt;
    while ((opt = getopt(argc, argv, "p:46dt:l:e:")) != -1) 
        {
        switch (opt) 
        {
        case 'p':
            newIp.srcPort = optarg;
            break;
        case '4':
            newIp.ipVersion = AF_INET;
            break;
        case '6':
            newIp.ipVersion = AF_INET6;
            break;
        case 'd':
            deamon = false;
            break;
        case 't':
            newIp.timeout = strtonum(optarg, 0, 1000,&newErr);
            if (newErr) {
                if (newErr) {
                    
                    if (strcmp(newErr, "too small") == 0) {
                        newIp.timeout = -1;
                        break;
                    }
                }
                errx(1, "timeout %s: %s \n", newErr, optarg);
            }
            break;
        case 'l':
            newIp.address = optarg;
            break;
        case 'e':
            newIp.tunnels[count] = optarg;
            count++;
            break;
        default: 
            err(1,"%c is not a valid character",opt);
            usage();   

        }

    }
    if (newIp.timeout == -5) 
        errx(1,"idle timeout value must be given");
    
    if (count == 0) 
        errx(1,"Atleast one tap device must be given");
    
    argc -= optind;
    argv += optind;
    switch (argc) {
    case 2:
            newIp.destPort = argv[1];     
    case 1:
            hostname = argv[0];
            break;
    default:
            usage();
    }
    if (newIp.srcPort == NULL) {
        newIp.srcPort = newIp.destPort;
    }
    destFd = connect_to_host(hostname, &newIp);
    connect_to_tap(&echods, &newIp, count, destFd);
    if (deamon)
        daemon(0, 0);
    event_init();
    event_setup(destFd,&newIp, &echods);
    free(newIp.tunnels);

    event_dispatch();

    return 0;
}




