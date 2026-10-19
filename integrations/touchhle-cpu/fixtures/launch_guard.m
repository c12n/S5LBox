/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Native macOS regression: real CFMessagePort + kernel flock, mock guest
 * application model. Requires root solely for the root-owned lease file. */
#import <Foundation/Foundation.h>
#include <sys/file.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "../../../tools/guest_hle_launch_guard.h"
extern void s5lbox_hle_guard_install(void);
static const char UUID[]="01234567-89AB-CDEF-0123-456789ABCDEF";
static unsigned failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); failures++; } } while (0)
@interface SBApplication : NSObject
@property(nonatomic) int pid;
@property(nonatomic,copy) NSString *path,*launchdJobLabel;
@property(nonatomic) BOOL acceptsLaunch,relaunchOnExit;
@property(nonatomic) unsigned launchCount;
- (signed char)launch;
- (void)exitedCommon;
- (void)launchFailed;
@end
@implementation SBApplication
- (signed char)launch { self.launchCount++; return self.acceptsLaunch; }
- (void)exitedCommon { self.pid=0; if (self.relaunchOnExit) [self launch]; }
- (void)launchFailed {}
- (void)dealloc { [_path release]; [_launchdJobLabel release]; [super dealloc]; }
@end
static SBApplication *application;
static NSArray *jobLabels;
@interface SBApplicationController : NSObject
+ (id)sharedInstance;
- (NSArray *)allApplications;
@end
@implementation SBApplicationController
+ (id)sharedInstance { static id value; if (!value) value=[self new]; return value; }
- (NSArray *)allApplications { return application?@[application]:@[]; }
@end
@interface SBLaunchdUtilities : NSObject
+ (NSArray *)allJobLabels;
@end
@implementation SBLaunchdUtilities
+ (NSArray *)allJobLabels { return jobLabels?:@[]; }
@end
static void pumpUntil(NSFileHandle *handle,NSTask *task,NSData **result) {
    __block NSData *data=nil;
    handle.readabilityHandler=^(NSFileHandle *file) {
        NSData *received=[file.availableData retain]; file.readabilityHandler=nil;
        @synchronized(task) { data=received; }
    };
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:10];
    while (deadline.timeIntervalSinceNow>0) {
        @synchronized(task) { if (data) break; }
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    handle.readabilityHandler=nil;
    @synchronized(task) {
        if (!data) { [task terminate]; CHECK(0); }
        *result=[data autorelease];
    }
}
static NSTask *client(NSString *executable,NSString *uuid,int expected,NSPipe **input) {
    NSTask *task=[[[NSTask alloc] init] autorelease]; NSPipe *out=[NSPipe pipe];
    *input=[NSPipe pipe]; task.executableURL=[NSURL fileURLWithPath:executable];
    task.arguments=@[@"--client",uuid]; task.standardOutput=out; task.standardInput=*input;
    CHECK([task launchAndReturnError:NULL]);
    NSData *reply=nil; pumpUntil(out.fileHandleForReading,task,&reply);
    NSString *text=[[[NSString alloc] initWithData:reply encoding:NSUTF8StringEncoding] autorelease];
    CHECK([text isEqualToString:[NSString stringWithFormat:@"%d\n",expected]]);
    return task;
}
static void finish(NSTask *task,NSPipe *input) {
    [input.fileHandleForWriting closeFile];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:10];
    while (task.running && deadline.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    if (task.running) { [task terminate]; CHECK(0); }
    else CHECK(task.terminationStatus==0);
}
int main(int argc,char **argv) {
    @autoreleasepool {
        if (argc==3 && !strcmp(argv[1],"--client")) {
            int fd=s5lbox_hle_launch_acquire(argv[2]),error=fd<0?errno:0;
            printf("%d\n",error); fflush(stdout);
            if (fd>=0) { char byte; (void)read(STDIN_FILENO,&byte,1); close(fd); }
            return 0;
        }
        CHECK(geteuid()==0); if (failures) return 1;
        s5lbox_hle_guard_install();
        NSString *exe=[NSString stringWithUTF8String:argv[0]],*uuid=[NSString stringWithUTF8String:UUID];
        NSPipe *input=nil; NSTask *task=client(exe,uuid,ENOENT,&input); finish(task,input);
        application=[SBApplication new]; application.acceptsLaunch=YES;
        application.path=[NSString stringWithFormat:@"/var/mobile/Applications/%@/Example.app",uuid];
        CHECK([application launch]); /* PID deliberately remains zero. */
        task=client(exe,uuid,EBUSY,&input); finish(task,input);
        [application exitedCommon];
        task=client(exe,uuid,0,&input);
        unsigned before=application.launchCount;
        CHECK(![application launch]); CHECK(application.launchCount==before);
        finish(task,input); CHECK([application launch]);
        [application launchFailed];
        application.pid=123;
        task=client(exe,uuid,EBUSY,&input); finish(task,input);
        application.pid=0; application.launchdJobLabel=@"retained-job"; jobLabels=@[@"retained-job"];
        task=client(exe,uuid,EBUSY,&input); finish(task,input);
        jobLabels=nil; application.launchdJobLabel=nil;
        application.relaunchOnExit=YES; [application exitedCommon];
        task=client(exe,uuid,EBUSY,&input); finish(task,input);
        application.relaunchOnExit=NO; [application exitedCommon];
        task=client(exe,uuid,0,&input); finish(task,input);
        application.acceptsLaunch=NO; CHECK(![application launch]);
        task=client(exe,uuid,0,&input); finish(task,input);
        [application release]; application=nil;
        printf("S5LBOX-HLE-LAUNCH-GUARD %s failures=%u\n",failures?"FAIL":"PASS",failures);
        return failures?1:0;
    }
}
