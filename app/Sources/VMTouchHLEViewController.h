// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
#import <UIKit/UIKit.h>
@class VMUSBTransport;
@interface VMTouchHLECoordinator : NSObject
- (instancetype)initWithTransport:(VMUSBTransport *)transport instanceID:(NSString *)identifier presenter:(UIViewController *)presenter;
// Main thread; safe to call every display tick. Internally rate limited.
- (void)tick:(BOOL)canRoute;
- (void)invalidate;
@end
@interface VMTouchHLEAppsViewController : UITableViewController
- (instancetype)initWithTransport:(VMUSBTransport *)transport instanceID:(NSString *)identifier;
@end
