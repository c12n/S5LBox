// Host service streams over the emulated USB cable. MIT licensed.
#import <Foundation/Foundation.h>
#include "usb_mux.h"

@interface VMUSBTransport : NSObject
/* Worker-thread only. Returns an owned nonblocking local socket, or -1.
 * No physical USB, network listener, host filesystem or guest disk access. */
- (int)openPort:(uint16_t)port timeout:(NSTimeInterval)timeout error:(NSError **)error;
- (int)openPort:(uint16_t)port timeout:(NSTimeInterval)timeout
      canceled:(BOOL (^)(void))canceled error:(NSError **)error;
/* Passive polling must never synthesize a Home press on a sleeping guest. */
- (int)openPort:(uint16_t)port timeout:(NSTimeInterval)timeout
      canceled:(BOOL (^)(void))canceled allowGuestWake:(BOOL)allowGuestWake error:(NSError **)error;
/* Owner emulator thread lifecycle; reset wakes every pending client. */
- (void)beginSession;
- (void)endSession:(NSString *)reason;
/* Cable reset: invalidate assigned streams, but preserve requests which have
 * not reached the guest. Their original timeout/cancellation still applies. */
- (void)reconnectSession:(NSString *)reason;
/* Owner-thread wake demand, including requests waiting for enumeration but
 * excluding passive polls. This query itself never mutates the guest. */
- (BOOL)hasServiceActivity;
- (void)pollWithMux:(usb_mux_t *)mux;
@end
