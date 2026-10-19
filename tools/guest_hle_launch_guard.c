/* Copyright (c) 2026 j0shua-SYSON. MIT licensed. Runs inside iPhone OS only. */
#include "guest_hle_launch_guard.h"
#include "guest_hle_cfmessageport.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

int s5lbox_hle_launch_acquire(const char uuid[37]) {
    int fd=open(S5LBOX_HLE_GUARD_PATH,O_CREAT|O_RDWR|O_NOFOLLOW,0644),error=0;
    if (fd<0) return -1;
    struct stat st;
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink!=1 ||
        (st.st_mode&0022) || fchmod(fd,0644)) { error=EACCES; goto fail; }
    if (flock(fd,LOCK_EX|LOCK_NB)) { error=EBUSY; goto fail; }
    CFMessagePortRef port=CFMessagePortCreateRemote(NULL,CFSTR(S5LBOX_HLE_GUARD_PORT));
    if (!port) { error=ENOTCONN; goto fail; }
    CFDataRef request=CFDataCreate(NULL,(const UInt8 *)uuid,36),response=NULL;
    if (!request) { CFRelease(port); error=ENOMEM; goto fail; }
    SInt32 status=CFMessagePortSendRequest(port,1,request,3,3,kCFRunLoopDefaultMode,&response);
    CFRelease(request); CFRelease(port);
    if (status || !response || CFDataGetLength(response)!=8 ||
        memcmp(CFDataGetBytePtr(response),"HLE1",4)) error=ENOTCONN;
    else {
        uint32_t result; memcpy(&result,CFDataGetBytePtr(response)+4,4);
        result=ntohl(result); error=result<=4096?(int)result:EPROTO;
    }
    if (response) CFRelease(response);
    if (!error) return fd;
fail:
    close(fd); errno=error; return -1;
}
