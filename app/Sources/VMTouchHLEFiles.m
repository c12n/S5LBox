// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
#import "VMTouchHLEFiles.h"
#import "VMUSBService.h"
#include <arpa/inet.h>

@interface VMTouchHLEFiles ()
- (int32_t)exchange:(const uint8_t *)request length:(uint32_t)length
           response:(uint8_t *)response capacity:(uint32_t)capacity count:(uint32_t *)count;
@end

static int32_t Exchange(void *context,const uint8_t *request,uint32_t length,
    uint8_t *response,uint32_t capacity,uint32_t *count) {
    @autoreleasepool {
        VMTouchHLEFiles *files=(__bridge VMTouchHLEFiles *)context;
        @try { return [files exchange:request length:length response:response capacity:capacity count:count]; }
        @catch (__unused NSException *exception) { [files close]; return -1; }
    }
}

@implementation VMTouchHLEFiles {
    VMUSBService *_service;
    NSString *_home,*_bundle;
    NSError *_error;
}
- (instancetype)initWithService:(VMUSBService *)service homeDirectory:(NSString *)home bundleDirectory:(NSString *)bundle {
    self=[super init];
    if (self) { _service=service; _home=[home copy]; _bundle=[bundle copy]; }
    return self;
}
- (NSError *)error { return _error; }
- (void)dealloc { [self close]; }
- (void)close { [_service close]; _service=nil; }
- (s5lbox_hle_live_config)configuration {
    s5lbox_hle_live_config config={0};
    config.abi_version=1; config.context=(__bridge void *)self; config.exchange=Exchange;
    config.home_directory=_home.UTF8String; config.bundle_directory=_bundle.UTF8String;
    return config;
}
- (int32_t)exchange:(const uint8_t *)request length:(uint32_t)length
           response:(uint8_t *)response capacity:(uint32_t)capacity count:(uint32_t *)count {
    if (count) *count=0;
    if (!_service || !request || !response || !count || length<5 || length>65540 || capacity<8) return -1;
    uint32_t declared; memcpy(&declared,request,4);
    if (ntohl(declared)!=length-4) { [self close]; return -1; }
    [_service setOperationDeadline:NSProcessInfo.processInfo.systemUptime+5];
    NSData *outgoing=[NSData dataWithBytes:request length:length];
    NSError *error=nil;
    if (![_service writeData:outgoing error:&error]) { _error=error; [self close]; return -1; }
    NSData *header=[_service readCount:4 error:&error];
    if (header.length!=4) { _error=error; [self close]; return -1; }
    memcpy(&declared,header.bytes,4); uint32_t size=ntohl(declared);
    if (size<4 || size>65540 || size>capacity-4) {
        _error=[NSError errorWithDomain:@"S5LBox.HLE.Files" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Invalid guest file response."}];
        [self close]; return -1;
    }
    NSData *body=[_service readCount:size error:&error];
    if (body.length!=size) { _error=error; [self close]; return -1; }
    memcpy(response,header.bytes,4); memcpy(response+4,body.bytes,size);
    *count=size+4;
    return 0;
}
@end
