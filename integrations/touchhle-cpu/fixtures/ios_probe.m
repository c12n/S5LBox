/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Ordinary UIKit test host. No JIT or private entitlements. */
#import <UIKit/UIKit.h>
#include <stdio.h>
int s5lbox_hle_fixture_main(int argc, char **argv);

@interface HLEProbeDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@property(nonatomic,strong) UILabel *status;
@end
@implementation HLEProbeDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)options;
    self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller=[UIViewController new];
    controller.view.backgroundColor=UIColor.systemBackgroundColor;
    self.status=[UILabel new]; self.status.numberOfLines=0;
    self.status.text=@"S5LBox HLE runtime test\nStarting…";
    self.status.textAlignment=NSTextAlignmentCenter;
    self.status.translatesAutoresizingMaskIntoConstraints=NO;
    [controller.view addSubview:self.status];
    [NSLayoutConstraint activateConstraints:@[
        [self.status.leadingAnchor constraintEqualToAnchor:controller.view.safeAreaLayoutGuide.leadingAnchor constant:24],
        [self.status.trailingAnchor constraintEqualToAnchor:controller.view.safeAreaLayoutGuide.trailingAnchor constant:-24],
        [self.status.centerYAnchor constraintEqualToAnchor:controller.view.centerYAnchor]]];
    self.window.rootViewController=controller; [self.window makeKeyAndVisible];
    application.idleTimerDisabled=YES;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            NSString *documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
            NSString *resources=NSBundle.mainBundle.resourcePath;
            NSString *guest=[resources stringByAppendingPathComponent:@"InterpreterSmoke.app"];
            NSString *log=[documents stringByAppendingPathComponent:@"native-probe.log"];
            freopen(log.fileSystemRepresentation,"w",stdout);
            freopen(log.fileSystemRepresentation,"a",stderr);
            setvbuf(stdout,NULL,_IONBF,0); setvbuf(stderr,NULL,_IONBF,0);
            puts("S5LBOX-HLE-UIKIT START");
            char *args[]={"hle-host-probe",(char *)guest.fileSystemRepresentation,
                (char *)resources.fileSystemRepresentation,(char *)documents.fileSystemRepresentation,NULL};
            int result=s5lbox_hle_fixture_main(4,args);
            fflush(stdout); fflush(stderr);
            dispatch_async(dispatch_get_main_queue(), ^{
                self.status.text=result?@"S5LBox HLE runtime test\nFAILED — see native-probe.log":
                    @"S5LBox HLE runtime test\nPASS\nLaunch, exit, fault recovery, stop and relaunch";
                application.idleTimerDisabled=NO;
            });
        }
    });
    return YES;
}
@end
int main(int argc,char **argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(HLEProbeDelegate.class)); }
}
