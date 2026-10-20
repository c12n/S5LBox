/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Guest-only, capability-authenticated icon routing. Separate from dpkg so
 * long package transactions cannot starve the SpringBoard launch heartbeat.
 * No shell, filesystem API, or client-provided executable paths. */
#include "guest_hle_launch_guard.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <mach/mach_time.h>
#define CAPABILITY "/private/var/lib/s5lbox-package-manager-v1/capability"
static double deadline;
static double uptime(void) {
    mach_timebase_info_data_t scale; mach_timebase_info(&scale);
    return (double)mach_absolute_time()*scale.numer/scale.denom/1e9;
}
static int exact(int fd,void *bytes,size_t count,int writing) {
    unsigned char *p=bytes;
    while (count) {
        struct pollfd poller={fd,writing?POLLOUT:POLLIN,0};
        double remaining=deadline-uptime();
        if (remaining<=0) return 0;
        int ready=poll(&poller,1,(int)(remaining*1000));
        if (ready<0 && errno==EINTR) continue;
        if (ready<=0) return 0;
        ssize_t n=writing?write(fd,p,count):read(fd,p,count);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return 0;
        p+=n; count-=(size_t)n;
    }
    return 1;
}
static void connection(int peer) {
    unsigned char hello[37],token[32]; struct stat st;
    int fd=open(CAPABILITY,O_RDONLY|O_NOFOLLOW); if (fd<0) return;
    int valid=!fstat(fd,&st) && S_ISREG(st.st_mode) && !st.st_uid &&
        st.st_nlink==1 && !(st.st_mode&0077) && st.st_size==32 && exact(fd,token,32,0);
    close(fd);
    if (!valid || !exact(peer,hello,sizeof hello,0)) return;
    unsigned mismatch=0;
    for (unsigned i=0;i<32;i++) mismatch|=token[i]^hello[4+i];
    if (mismatch || memcmp(hello,"SPM2",4) || hello[36]!='T') return;
    uint32_t count;
    if (!exact(peer,&count,4,0)) return;
    count=ntohl(count);
    if (count<27 || count>HLE_ROUTE_MAX_REQUEST) return;
    unsigned char *request=malloc(count),reply[HLE_ROUTE_MAX_REPLY];
    if (!request) return;
    int length=exact(peer,request,count,0)?s5lbox_hle_route_exchange(request,count,reply):-1;
    free(request);
    const char *message="Guest app support is restarting. Wait for the respring, then retry.";
    char kind=length<0?'E':'T';
    uint32_t frame=htonl(1+(uint32_t)(length<0?strlen(message):(size_t)length));
    if (exact(peer,&frame,4,1) && exact(peer,&kind,1,1))
        exact(peer,length<0?(void *)message:reply,length<0?strlen(message):(size_t)length,1);
}
int main(void) {
    if (geteuid()) return 1;
    umask(0077); signal(SIGPIPE,SIG_IGN);
    int listener=socket(AF_INET,SOCK_STREAM,0),yes=1; if (listener<0) return 1;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
    struct sockaddr_in address; memset(&address,0,sizeof address);
    address.sin_family=AF_INET; address.sin_port=htons(64322); address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (bind(listener,(struct sockaddr *)&address,sizeof address) || listen(listener,2)) return 1;
    for (;;) {
        int peer=accept(listener,NULL,NULL);
        if (peer<0) { if (errno==EINTR) continue; return 1; }
        deadline=uptime()+8; connection(peer); close(peer);
    }
}
