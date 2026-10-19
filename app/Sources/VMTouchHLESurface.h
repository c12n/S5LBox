// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
#import <Foundation/Foundation.h>
#include "../../integrations/touchhle-cpu/s5lbox_hle.h"

@interface VMTouchHLESurface : NSObject
- (instancetype)initWithWidth:(uint32_t)width height:(uint32_t)height;
- (s5lbox_hle_presentation)configuration;
// Main-thread consumer; one immutable top-down BGRA frame, never a growing queue.
- (NSData *)takeFrame;
@property(nonatomic,readonly) uint32_t width,height;
@property(atomic,readonly) NSUInteger frameCount;
// Main thread produces input; the runtime worker drains it.
- (BOOL)enqueueInput:(s5lbox_hle_input_event)event;
@property(nonatomic,copy) void (^textInputChanged)(BOOL active);
@property(nonatomic,copy) void (^idleTimerChanged)(BOOL enabled);
// Call on the runtime worker only, after the run returns, even after a fault.
- (void)finish;
@end
