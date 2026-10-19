// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
#import <UIKit/UIKit.h>
@class VMUSBTransport;
@interface VMTouchHLEAppsViewController : UITableViewController
- (instancetype)initWithTransport:(VMUSBTransport *)transport instanceID:(NSString *)identifier;
@end
