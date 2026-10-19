// Credential publication state, using the actual bridge implementation.
// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import "../Sources/VMPackageBridge.m"
#import "VMTouchHLEFiles.h"

static NSURL *testDirectory;
@interface NSFileManager (PackageTestPaths)
- (NSArray<NSURL *> *)packageTestURLs:(NSSearchPathDirectory)directory inDomains:(NSSearchPathDomainMask)domain;
@end
@implementation NSFileManager (PackageTestPaths)
- (NSArray<NSURL *> *)packageTestURLs:(NSSearchPathDirectory)directory inDomains:(NSSearchPathDomainMask)domain {
    if (directory == NSApplicationSupportDirectory) return @[testDirectory];
    return [self packageTestURLs:directory inDomains:domain];
}
@end

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while (0)

// Exercise status framing without a device, sockets or a mutable guest disk.
@interface PackageStatusFixture : VMUSBService
@property(nonatomic, strong) NSMutableData *bytes;
@property(nonatomic, strong) NSData *sent;
@property(nonatomic) NSUInteger writes, closes;
@property(nonatomic) BOOL failWrite;
@end
@implementation PackageStatusFixture
- (NSData *)readCount:(NSUInteger)count error:(NSError **)error {
    if (count > self.bytes.length) {
        if (error) *error=VMPackageError(@"Short fixture response.");
        return nil;
    }
    NSData *part=[self.bytes subdataWithRange:NSMakeRange(0,count)];
    [self.bytes replaceBytesInRange:NSMakeRange(0,count) withBytes:NULL length:0];
    return part;
}
- (BOOL)writeData:(NSData *)data error:(NSError **)error {
    self.writes++; self.sent=data;
    if (self.failWrite && error) *error=VMPackageError(@"Lost file reply.");
    return !self.failWrite;
}
- (void)close { self.closes++; }
@end
@interface PackageFilesBridge : VMPackageBridge
@property(nonatomic,strong) PackageStatusFixture *fixture;
@end
@implementation PackageFilesBridge
- (VMUSBService *)connect:(char)operation error:(NSError **)error {
    (void)error; CHECK(operation=='H'); return self.fixture;
}
@end
static PackageStatusFixture *FileReply(uint32_t length, uint32_t error) {
    PackageStatusFixture *fixture=[PackageStatusFixture new];
    uint32_t reply[2]={htonl(length),htonl(error)};
    fixture.bytes=[NSMutableData dataWithBytes:reply length:sizeof reply];
    return fixture;
}
static void TestLiveFiles(void) {
    NSString *uuid=@"01234567-89AB-CDEF-0123-456789ABCDEF";
    PackageFilesBridge *bridge=[PackageFilesBridge new];
    bridge.fixture=FileReply(4,0);
    NSError *error=nil;
    CHECK([bridge openLiveFilesForContainer:@"../invalid" error:&error]==nil);
    CHECK(bridge.fixture.writes==0 && error!=nil);
    error=nil;
    CHECK([bridge openLiveFilesForContainer:uuid error:&error]==bridge.fixture);
    CHECK(error==nil && bridge.fixture.writes==1 && bridge.fixture.closes==0);
    CHECK([bridge.fixture.sent isEqual:[uuid dataUsingEncoding:NSUTF8StringEncoding]]);
    for (unsigned test=0;test<3;test++) {
        bridge.fixture=FileReply(test==1?5:4,test==0?EBUSY:0);
        if (test==2) [bridge.fixture.bytes setLength:7];
        error=nil;
        CHECK([bridge openLiveFilesForContainer:uuid error:&error]==nil);
        CHECK(bridge.fixture.closes==1 && error!=nil);
    }
    const uint8_t request[]={0,0,0,1,7}; uint8_t response[16]; uint32_t count=99;
    for (unsigned test=0;test<5;test++) {
        PackageStatusFixture *fixture=FileReply(test==2?65541:4,0);
        if (test==1) fixture.failWrite=YES;
        if (test==3) [fixture.bytes setLength:7];
        if (test==4) [fixture.bytes setLength:3];
        VMTouchHLEFiles *files=[[VMTouchHLEFiles alloc] initWithService:fixture
            homeDirectory:[@"/var/mobile/Applications/" stringByAppendingString:uuid] bundleDirectory:@"Fixture.app"];
        s5lbox_hle_live_config config=files.configuration;
        CHECK(config.abi_version==1 && !strcmp(config.bundle_directory,"Fixture.app"));
        int result=config.exchange(config.context,request,sizeof request,response,sizeof response,&count);
        if (!test) { CHECK(result==0 && count==8 && !memcmp(response,FileReply(4,0).bytes.bytes,8)); }
        else {
            CHECK(result!=0 && count==0 && files.error!=nil && fixture.closes==1);
            CHECK(config.exchange(config.context,request,sizeof request,response,sizeof response,&count)!=0);
            CHECK(fixture.writes==1); // Never reconnect/replay an uncertain mutation.
        }
        [files close];
        CHECK(fixture.closes==1);
    }
}
@interface PackageStatusBridge : VMPackageBridge
@property(nonatomic, strong) PackageStatusFixture *fixture;
@end
@implementation PackageStatusBridge
- (VMUSBService *)connect:(char)operation error:(NSError **)error {
    (void)error; CHECK(operation=='S'); return self.fixture;
}
@end
static void StatusFrame(PackageStatusFixture *fixture, char kind, NSData *data) {
    uint32_t size=htonl((uint32_t)data.length+1);
    [fixture.bytes appendBytes:&size length:4];
    [fixture.bytes appendBytes:&kind length:1];
    [fixture.bytes appendData:data];
}
static void TestFirstBootReadiness(void) {
    PackageStatusBridge *bridge=[PackageStatusBridge new];
    bridge.fixture=[PackageStatusFixture new];
    bridge.fixture.bytes=[NSMutableData data];
    NSString *pending=@"Jailbreak setup is still finishing. Wait for it to complete, then refresh Packages and retry.";
    StatusFrame(bridge.fixture,'E',[pending dataUsingEncoding:NSUTF8StringEncoding]);
    NSError *error=nil;
    CHECK([bridge status:&error]==nil && bridge.setupPending);
    CHECK([error.localizedDescription isEqual:pending]);
    CHECK(!bridge.finishReady && bridge.finishAction==0);

    NSData *status=[@"Package: fixture\nStatus: install ok installed\n\n" dataUsingEncoding:NSUTF8StringEncoding];
    const unsigned char finish[2]={0,1};
    StatusFrame(bridge.fixture,'F',[NSData dataWithBytes:finish length:2]);
    StatusFrame(bridge.fixture,'S',status);
    error=nil;
    CHECK([[bridge status:&error] isEqual:status] && error==nil);
    CHECK(!bridge.setupPending && bridge.finishReady);

    StatusFrame(bridge.fixture,'E',[@"Database unavailable." dataUsingEncoding:NSUTF8StringEncoding]);
    CHECK([bridge status:&error]==nil && !bridge.setupPending);
    CHECK([error.localizedDescription containsString:@"Cannot connect to iPhone OS."]);
}
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc != 2) return 2;
        TestFirstBootReadiness();
        TestLiveFiles();
        testDirectory = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]] isDirectory:YES];
        method_exchangeImplementations(class_getInstanceMethod(NSFileManager.class,@selector(URLsForDirectory:inDomains:)),
            class_getInstanceMethod(NSFileManager.class,@selector(packageTestURLs:inDomains:)));
        NSString *identifier = @"0123456789abcdef";
        CHECK(![VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK(PackageSetupPending(identifier,YES,NULL));
        NSData *token = Capability(identifier,YES,NULL);
        CHECK(token.length == 32);
        CHECK(![VMPackageBridge hasCapabilityForInstance:identifier]);
        // Retry after a failed or interrupted publication keeps the same token.
        CHECK(PackageSetupPending(identifier,YES,NULL));
        CHECK([Capability(identifier,YES,NULL) isEqualToData:token]);
        CHECK(![VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK(PackageSetupPending(identifier,NO,NULL));
        CHECK([VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK(PackageSetupPending(identifier,NO,NULL));
        // Existing installations without a marker remain compatible. An
        // attempted repair becomes pending until it is verified again.
        CHECK(PackageSetupPending(identifier,YES,NULL));
        CHECK(![VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK([Capability(identifier,NO,NULL) isEqualToData:token]);
        CHECK(PackageSetupPending(identifier,NO,NULL));
        CHECK([VMPackageBridge hasCapabilityForInstance:identifier]);
        // Disk publication readiness and first live connection are distinct.
        // A manually opened workspace must keep the first-start allowance even
        // after its controller was dismissed or the host app was relaunched.
        [VMPackageBridge noteSuccessfulConnectionForInstance:identifier];
        CHECK(![VMPackageBridge awaitingFirstConnectionForInstance:identifier]);
        AwaitFirstConnection(identifier);
        CHECK([VMPackageBridge awaitingFirstConnectionForInstance:identifier]);
        CHECK([NSUserDefaults.standardUserDefaults boolForKey:FirstConnectionKey(identifier)]);
        CHECK([VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK(![VMPackageBridge awaitingFirstConnectionForInstance:@"fedcba9876543210"]);
        CHECK(PackageSetupPending(identifier,YES,NULL));
        CHECK([VMPackageBridge awaitingFirstConnectionForInstance:identifier]);
        [VMPackageBridge noteSuccessfulConnectionForInstance:identifier];
        CHECK(![VMPackageBridge awaitingFirstConnectionForInstance:identifier]);
        CHECK(![VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK(PackageSetupPending(identifier,NO,NULL));
        CHECK([VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK(![VMPackageBridge awaitingFirstConnectionForInstance:identifier]);
        // Invalid identities never write a hint or affect another machine.
        AwaitFirstConnection(@"../invalid");
        CHECK(![VMPackageBridge awaitingFirstConnectionForInstance:@"../invalid"]);
        [VMPackageBridge noteSuccessfulConnectionForInstance:@"../invalid"];
        NSDictionary *attributes = [NSFileManager.defaultManager attributesOfItemAtPath:CapabilityURL(identifier,NULL).path error:NULL];
        CHECK([attributes[NSFilePosixPermissions] unsignedIntValue] == 0600);
        CHECK(Capability(@"../invalid",YES,NULL) == nil);
        CHECK(!PackageSetupPending(@"../invalid",YES,NULL));
        CHECK(![VMPackageBridge hasCapabilityForInstance:@"../invalid"]);
        NSData *invalid = [@"bad" dataUsingEncoding:NSUTF8StringEncoding];
        CHECK([invalid writeToURL:CapabilityURL(identifier,NULL) atomically:YES]);
        CHECK(Capability(identifier,YES,NULL) == nil);
        CHECK(![VMPackageBridge hasCapabilityForInstance:identifier]);
        CHECK([[NSData dataWithContentsOfURL:CapabilityURL(identifier,NULL)] isEqualToData:invalid]);
        printf("Package setup credentials: %u checks, %u failures\n",checks,failures);
        return failures ? 1 : 0;
    }
}
