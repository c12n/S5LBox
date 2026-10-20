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
#include <stdlib.h>

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

int s5lbox_hle_route_exchange(const unsigned char *bytes,size_t count,
        unsigned char reply[HLE_ROUTE_MAX_REPLY]) {
    if (count<27 || count>HLE_ROUTE_MAX_REQUEST) { errno=EINVAL; return -1; }
    int fd=open(S5LBOX_HLE_ROUTE_PATH,O_CREAT|O_RDWR|O_NOFOLLOW,0644),error=EACCES,result=-1,locked=0;
    if (fd<0) return -1;
    struct stat st; struct flock lock; memset(&lock,0,sizeof lock);
    lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
    /* A POSIX write lock requires a writable descriptor. Unlike flock,
     * mobile cannot manufacture this proof through its read-only descriptor. */
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink!=1 ||
        (st.st_mode&0022) || fchmod(fd,0644) || fcntl(fd,F_SETLK,&lock)) goto done;
    locked=1;
    unsigned char *request=malloc(32+count);
    if (!request) { error=ENOMEM; goto done; }
    for (unsigned i=0;i<8;i++) { uint32_t random=arc4random(); memcpy(request+i*4,&random,4); }
    memcpy(request+32,bytes,count);
    if (ftruncate(fd,0) || write(fd,request,32+count)!=(ssize_t)(32+count)) {
        free(request); error=EIO; goto done;
    }
    CFMessagePortRef port=CFMessagePortCreateRemote(NULL,CFSTR(S5LBOX_HLE_GUARD_PORT));
    CFDataRef data=CFDataCreate(NULL,request,(CFIndex)(32+count)),response=NULL;
    free(request);
    SInt32 status=port&&data?CFMessagePortSendRequest(port,2,data,3,3,kCFRunLoopDefaultMode,&response):-1;
    if (port) CFRelease(port);
    if (data) CFRelease(data);
    error=ENOTCONN;
    if (!status && response && CFDataGetLength(response)>=18 &&
        CFDataGetLength(response)<=HLE_ROUTE_MAX_REPLY && !memcmp(CFDataGetBytePtr(response),"HLR1",4)) {
        result=(int)CFDataGetLength(response); memcpy(reply,CFDataGetBytePtr(response),(size_t)result);
    }
    if (response) CFRelease(response);
done:
    /* Closing releases the write lock even after a crash; bytes alone never
     * authorize an operation. Leave no old authorized request on disk. */
    if (locked) (void)ftruncate(fd,0);
    close(fd); errno=error; return result;
}
