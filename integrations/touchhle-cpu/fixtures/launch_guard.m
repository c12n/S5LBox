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
@property(nonatomic,copy) NSString *path,*launchdJobLabel,*bundleIdentifier;
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
- (void)dealloc { [_path release]; [_launchdJobLabel release]; [_bundleIdentifier release]; [super dealloc]; }
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
static unsigned iconLaunches,activations;
@interface SBApplicationIcon : NSObject
- (SBApplication *)application;
- (void)launch;
@end
@implementation SBApplicationIcon
- (SBApplication *)application { return application; }
- (void)launch { iconLaunches++; }
@end
@interface SBUIController : NSObject
+ (id)sharedInstance;
- (void)activateApplicationAnimated:(SBApplication *)app;
@end
@implementation SBUIController
+ (id)sharedInstance { static id instance; if (!instance) instance=[self new]; return instance; }
- (void)activateApplicationAnimated:(SBApplication *)app { CHECK(app==application); activations++; }
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
    CHECK(([text isEqualToString:[NSString stringWithFormat:@"%d\n",expected]]));
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
static NSString *route(NSString *exe,unsigned op,unsigned session,uint64_t ticket,NSString *name) {
    NSTask *task=[[[NSTask alloc] init] autorelease]; NSPipe *out=[NSPipe pipe];
    task.executableURL=[NSURL fileURLWithPath:exe];
    task.arguments=@[@"--route",@(op).stringValue,@(session).stringValue,@(ticket).stringValue,name?:@""];
    task.standardOutput=out; CHECK([task launchAndReturnError:NULL]);
    NSData *reply=nil; pumpUntil(out.fileHandleForReading,task,&reply);
    [task waitUntilExit]; CHECK(task.terminationStatus==0);
    return [[[NSString alloc] initWithData:reply encoding:NSUTF8StringEncoding] autorelease];
}
int main(int argc,char **argv) {
    @autoreleasepool {
        if (argc==6 && !strcmp(argv[1],"--route")) {
            unsigned char p[HLE_ROUTE_MAX_REQUEST]={0},reply[HLE_ROUTE_MAX_REPLY];
            p[0]=(unsigned char)atoi(argv[2]); p[1]=(unsigned char)atoi(argv[3]);
            uint64_t ticket=strtoull(argv[4],NULL,10);
            for (unsigned i=0;i<8;i++) p[17+i]=(unsigned char)(ticket>>(56-8*i));
            size_t n=strlen(argv[5]); p[26]=n?1:0;
            p[27]=(unsigned char)(n>>8); p[28]=(unsigned char)n; memcpy(p+29,argv[5],n);
            int result=s5lbox_hle_route_exchange(p,n?29+n:27,reply);
            ticket=0;
            if (result>=18) for (unsigned i=0;i<8;i++) ticket=(ticket<<8)|reply[8+i];
            printf("%d %llu\n",result>=18?reply[7]:errno,(unsigned long long)ticket);
            return 0;
        }
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
        application.bundleIdentifier=@"example.app";
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
        /* Same CF port as production, with a root broker in another process.
         * An icon queues only while the authorized heartbeat is live. */
        SBApplicationIcon *icon=[SBApplicationIcon new];
        [icon launch]; CHECK(iconLaunches==1);
        CHECK([route(exe,1,1,0,@"example.app") isEqual:@"0 0\n"]);
        [icon launch]; [icon launch]; CHECK(iconLaunches==1);
        CHECK([route(exe,1,1,0,@"example.app") isEqual:@"0 1\n"]);
        CHECK([route(exe,1,1,0,@"example.app") isEqual:@"0 1\n"]);
        CHECK([route(exe,2,1,1,nil) isEqual:@"0 0\n"]);
        CHECK([route(exe,3,1,0,@"example.app") isEqual:@"0 0\n"] && activations==1);
        task=client(exe,uuid,0,&input);
        CHECK(![route(exe,3,1,0,@"example.app") isEqual:@"0 0\n"] && activations==1);
        finish(task,input);
        CHECK([route(exe,4,1,0,nil) isEqual:@"0 0\n"]);
        [icon launch]; CHECK(iconLaunches==2);
        [icon release];
        [application release]; application=nil;
        printf("S5LBOX-HLE-LAUNCH-GUARD %s failures=%u\n",failures?"FAIL":"PASS",failures);
        return failures?1:0;
    }
}
