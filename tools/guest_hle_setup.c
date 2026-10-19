/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Fixed-purpose guest package maintainer tool. Edits only SpringBoard's
 * launch environment, using its current dictionary rather than a template.
 * It never opens an emulated disk image or alters the physical host's OS. */
#include <CoreFoundation/CFPropertyList.h>
#include <CoreFoundation/CFStream.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef S5LBOX_HLE_SPRINGBOARD_JOB
#define S5LBOX_HLE_SPRINGBOARD_JOB "/System/Library/LaunchDaemons/com.apple.SpringBoard.plist"
#endif
#define LIBRARY "/usr/lib/s5lbox-hle-launch-v1.dylib"
static int exact(int fd,void *bytes,size_t size,int writing) {
    unsigned char *p=bytes;
    while (size) {
        ssize_t n=writing?write(fd,p,size):read(fd,p,size);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return 0;
        p+=n; size-=(size_t)n;
    }
    return 1;
}
static int same(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_uid==b->st_uid && a->st_gid==b->st_gid && a->st_mode==b->st_mode &&
        a->st_mtimespec.tv_sec==b->st_mtimespec.tv_sec && a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec;
}
static void request_reload(void) {
    const char *cydia=getenv("CYDIA"); char *end=NULL;
    if (!cydia || *cydia<'0' || *cydia>'9') return;
    errno=0; long fd=strtol(cydia,&end,10);
    if (errno || fd<3 || fd>1023 || strcmp(end," 1")) return;
    const char message[]="finish:reload\n";
    (void)exact((int)fd,(void *)message,sizeof message-1,1);
}
int main(int argc,char **argv) {
    if (argc!=2 || (strcmp(argv[1],"enable") && strcmp(argv[1],"disable")) || geteuid()!=0) return 1;
    int enable=!strcmp(argv[1],"enable"),result=1,fd=-1,out=-1;
    unsigned char *bytes=NULL; struct stat before,after;
    CFReadStreamRef stream=NULL; CFPropertyListRef original=NULL;
    CFMutableDictionaryRef job=NULL,env=NULL; CFMutableArrayRef libraries=NULL;
    CFDataRef replacement=NULL; CFStringRef joined=NULL;
    char temporary[sizeof S5LBOX_HLE_SPRINGBOARD_JOB+16]={0};
    fd=open(S5LBOX_HLE_SPRINGBOARD_JOB,O_RDONLY|O_NOFOLLOW);
    if (fd<0 || fstat(fd,&before) || !S_ISREG(before.st_mode) || before.st_nlink!=1 ||
        before.st_uid!=0 || (before.st_mode&0022) || before.st_size<1 || before.st_size>65536) goto done;
    bytes=malloc((size_t)before.st_size);
    if (!bytes || !exact(fd,bytes,(size_t)before.st_size,0)) goto done;
    stream=CFReadStreamCreateWithBytesNoCopy(NULL,bytes,(CFIndex)before.st_size,kCFAllocatorNull);
    if (!stream || !CFReadStreamOpen(stream)) goto done;
    original=CFPropertyListCreateFromStream(NULL,stream,(CFIndex)before.st_size,kCFPropertyListImmutable,NULL,NULL);
    if (!original || CFGetTypeID(original)!=CFDictionaryGetTypeID()) goto done;
    const void *label=CFDictionaryGetValue(original,CFSTR("Label"));
    const void *user=CFDictionaryGetValue(original,CFSTR("UserName"));
    CFArrayRef args=CFDictionaryGetValue(original,CFSTR("ProgramArguments"));
    if (!label || !CFEqual(label,CFSTR("com.apple.SpringBoard")) ||
        !user || !CFEqual(user,CFSTR("mobile")) || !args || CFGetTypeID(args)!=CFArrayGetTypeID() ||
        CFArrayGetCount(args)<1 || !CFEqual(CFArrayGetValueAtIndex(args,0),
            CFSTR("/System/Library/CoreServices/SpringBoard.app/SpringBoard"))) goto done;
    CFDictionaryRef currentEnv=CFDictionaryGetValue(original,CFSTR("EnvironmentVariables"));
    if (currentEnv && CFGetTypeID(currentEnv)!=CFDictionaryGetTypeID()) goto done;
    env=currentEnv?CFDictionaryCreateMutableCopy(NULL,0,currentEnv):
        CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if (!env) goto done;
    CFStringRef current=CFDictionaryGetValue(env,CFSTR("DYLD_INSERT_LIBRARIES"));
    if (current && CFGetTypeID(current)!=CFStringGetTypeID()) goto done;
    libraries=CFArrayCreateMutable(NULL,0,&kCFTypeArrayCallBacks);
    if (!libraries) goto done;
    if (current && CFStringGetLength(current)) {
        CFArrayRef parts=CFStringCreateArrayBySeparatingStrings(NULL,current,CFSTR(":"));
        if (!parts) goto done;
        for (CFIndex i=0;i<CFArrayGetCount(parts);i++) {
            CFStringRef item=CFArrayGetValueAtIndex(parts,i);
            if (!CFEqual(item,CFSTR(LIBRARY))) CFArrayAppendValue(libraries,item);
        }
        CFRelease(parts);
    }
    if (enable) CFArrayAppendValue(libraries,CFSTR(LIBRARY));
    if (CFArrayGetCount(libraries)) {
        joined=CFStringCreateByCombiningStrings(NULL,libraries,CFSTR(":"));
        if (!joined) goto done;
        CFDictionarySetValue(env,CFSTR("DYLD_INSERT_LIBRARIES"),joined);
    } else CFDictionaryRemoveValue(env,CFSTR("DYLD_INSERT_LIBRARIES"));
    job=CFDictionaryCreateMutableCopy(NULL,0,original); if (!job) goto done;
    if (CFDictionaryGetCount(env)) CFDictionarySetValue(job,CFSTR("EnvironmentVariables"),env);
    else CFDictionaryRemoveValue(job,CFSTR("EnvironmentVariables"));
    if (CFEqual(job,original)) { result=0; goto done; }
    replacement=CFPropertyListCreateXMLData(NULL,job);
    if (!replacement || CFDataGetLength(replacement)>65536) goto done;
    if (snprintf(temporary,sizeof temporary,"%s.XXXXXX",S5LBOX_HLE_SPRINGBOARD_JOB)>=(int)sizeof temporary) goto done;
    out=mkstemp(temporary); if (out<0) { temporary[0]=0; goto done; }
    if (fchmod(out,before.st_mode&0777) || fchown(out,before.st_uid,before.st_gid) ||
        !exact(out,(void *)CFDataGetBytePtr(replacement),(size_t)CFDataGetLength(replacement),1) || fsync(out)) goto done;
    if (close(out)) { out=-1; goto done; } out=-1;
    if (fstat(fd,&after) || !same(&before,&after) ||
        lstat(S5LBOX_HLE_SPRINGBOARD_JOB,&after) || !same(&before,&after)) goto done;
    if (rename(temporary,S5LBOX_HLE_SPRINGBOARD_JOB)) goto done;
    temporary[0]=0;
    /* A fixed parent, never a client-provided path. Persist the replacement
     * directory entry before reporting success to dpkg. */
    char directory[sizeof S5LBOX_HLE_SPRINGBOARD_JOB]; strcpy(directory,S5LBOX_HLE_SPRINGBOARD_JOB);
    char *slash=strrchr(directory,'/'); if (!slash) goto done; *slash=0;
    int parent=open(directory,O_RDONLY); if (parent<0) goto done;
    int synced=fsync(parent); int sync_error=errno; close(parent);
    if (synced && sync_error!=EINVAL) goto done;
    result=0;
done:
    if (out>=0) close(out);
    if (temporary[0]) unlink(temporary);
    if (fd>=0) close(fd);
    if (stream) { CFReadStreamClose(stream); CFRelease(stream); }
    if (original) CFRelease(original);
    if (job) CFRelease(job);
    if (env) CFRelease(env);
    if (libraries) CFRelease(libraries);
    if (joined) CFRelease(joined);
    if (replacement) CFRelease(replacement);
    free(bytes);
    if (result) fputs("SpringBoard launch configuration was not safely updated.\n",stderr);
    else request_reload();
    return result;
}
