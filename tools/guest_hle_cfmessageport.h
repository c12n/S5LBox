/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Public pre-GCD CFMessagePort ABI. The community 3.0 SDK's newer umbrella
 * header imports missing dispatch headers; this guest uses only run loops. */
#ifndef S5LBOX_GUEST_CFMESSAGEPORT_H
#define S5LBOX_GUEST_CFMESSAGEPORT_H
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFRunLoop.h>
typedef struct __CFMessagePort *CFMessagePortRef;
typedef CFDataRef (*CFMessagePortCallBack)(CFMessagePortRef,SInt32,CFDataRef,void *);
typedef struct {
    CFIndex version; void *info;
    const void *(*retain)(const void *);
    void (*release)(const void *);
    CFStringRef (*copyDescription)(const void *);
} CFMessagePortContext;
extern CFMessagePortRef CFMessagePortCreateRemote(CFAllocatorRef,CFStringRef);
extern CFMessagePortRef CFMessagePortCreateLocal(CFAllocatorRef,CFStringRef,
    CFMessagePortCallBack,CFMessagePortContext *,Boolean *);
extern SInt32 CFMessagePortSendRequest(CFMessagePortRef,SInt32,CFDataRef,
    CFTimeInterval,CFTimeInterval,CFStringRef,CFDataRef *);
extern CFRunLoopSourceRef CFMessagePortCreateRunLoopSource(CFAllocatorRef,CFMessagePortRef,CFIndex);
#endif
