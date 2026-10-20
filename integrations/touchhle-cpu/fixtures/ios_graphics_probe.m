// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
#import <UIKit/UIKit.h>
#include <stdlib.h>
#import "../../../app/Sources/VMTouchHLESurface.h"
#include <stdio.h>
#include <math.h>
@interface HLEProbeDisplay : UIView
@property(nonatomic,weak) VMTouchHLESurface *surface;
@end
@implementation HLEProbeDisplay
- (void)touches:(NSSet<UITouch *> *)touches kind:(uint32_t)kind {
    if (self.bounds.size.width<=0 || self.bounds.size.height<=0) return;
    for (UITouch *touch in touches) {
        CGPoint point=[touch locationInView:self];
        s5lbox_hle_input_event event={0}; event.kind=kind;
        event.finger=(int64_t)(uintptr_t)(__bridge void *)touch;
        event.x=(float)(point.x/self.bounds.size.width); event.y=(float)(point.y/self.bounds.size.height);
        [self.surface enqueueInput:event];
    }
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:1]; }
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:2]; }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:3]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:9]; }
@end
static void ReleaseFrame(void *context,const void *data,size_t size) { (void)data; (void)size; CFRelease(context); }
@interface HLEGraphicsProbe : UIViewController
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) HLEProbeDisplay *display;
@property(nonatomic,strong) UIButton *button;
@property(nonatomic,strong) UIButton *home;
@property(nonatomic,strong) VMTouchHLESurface *surface;
@property(nonatomic,strong) CADisplayLink *link;
@property(nonatomic) const s5lbox_hle_control *control;
@property(nonatomic) unsigned verifiedColors;
@end
@implementation HLEGraphicsProbe
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor=UIColor.systemBackgroundColor;
    self.status=[UILabel new]; self.status.numberOfLines=0; self.status.textAlignment=NSTextAlignmentCenter;
    self.display=[HLEProbeDisplay new]; self.display.backgroundColor=UIColor.blackColor;
    self.display.multipleTouchEnabled=YES; self.display.layer.magnificationFilter=kCAFilterNearest;
    self.button=[UIButton buttonWithType:UIButtonTypeSystem];
    [self.button addTarget:self action:@selector(toggle) forControlEvents:UIControlEventTouchUpInside];
    self.home=[UIButton buttonWithType:UIButtonTypeSystem];
    [self.home setTitle:@"Home" forState:UIControlStateNormal];
    [self.home addTarget:self action:@selector(homePressed) forControlEvents:UIControlEventTouchUpInside];
    for (UIView *view in @[self.status,self.display,self.button,self.home]) [self.view addSubview:view];
    self.link=[CADisplayLink displayLinkWithTarget:self selector:@selector(frame)];
    [self.link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(inactive) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(active) name:UIApplicationDidBecomeActiveNotification object:nil];
    [self toggle];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews]; CGRect safe=self.view.safeAreaLayoutGuide.layoutFrame;
    self.status.frame=CGRectMake(safe.origin.x+8,safe.origin.y,safe.size.width-16,60);
    self.home.frame=CGRectMake(safe.origin.x,safe.origin.y+safe.size.height-48,80,44);
    self.button.frame=CGRectMake(safe.origin.x+80,safe.origin.y+safe.size.height-48,safe.size.width-80,44);
    CGFloat height=MIN(safe.size.height-132,(safe.size.width-24)*1.5),width=height/1.5;
    self.display.frame=CGRectMake(CGRectGetMidX(safe)-width/2,safe.origin.y+72,width,height);
}
- (void)inactive {
    if (self.control) s5lbox_hle_control_pause(self.control,true);
    [self.surface setGraphicsSuspended:YES];
}
- (void)active {
    [self.surface setGraphicsSuspended:NO];
    if (self.control) s5lbox_hle_control_pause(self.control,false);
}
- (void)homePressed {
    if (!self.control) return;
    s5lbox_hle_input_event event={0}; event.kind=8;
    [self.surface enqueueInput:event];
}
- (void)toggle {
    if (self.control) { s5lbox_hle_control_stop(self.control); return; }
    self.surface=[[VMTouchHLESurface alloc] initWithWidth:320 height:480]; self.display.surface=self.surface;
    self.verifiedColors=0;
    self.control=s5lbox_hle_control_create();
    const s5lbox_hle_control *control=self.control; VMTouchHLESurface *surface=self.surface;
    [self.button setTitle:@"Stop graphics test" forState:UIControlStateNormal]; self.status.text=@"Native graphics starting";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            NSString *documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
            NSString *resources=NSBundle.mainBundle.resourcePath;
            NSString *guest=[resources stringByAppendingPathComponent:@"HLEVisual.app"];
            s5lbox_hle_config config={0}; config.abi_version=1;
            config.app_path=guest.fileSystemRepresentation; config.resource_directory=resources.fileSystemRepresentation;
            config.data_directory=documents.fileSystemRepresentation;
            s5lbox_hle_presentation presentation=surface.configuration; s5lbox_hle_outcome result={0};
            float acceleration[3]={0};
            presentation.acceleration(presentation.context,acceleration);
            BOOL motionOK=acceleration[0]==0 && acceleration[1]==0 && acceleration[2]==-1;
            [surface setAccelerationX:0.25f y:-0.5f z:0.75f];
            [surface setAccelerationX:NAN y:1 z:1]; // Invalid samples cannot poison the guest.
            presentation.acceleration(presentation.context,acceleration);
            motionOK=motionOK && acceleration[0]==0.25f && acceleration[1]==-0.5f && acceleration[2]==0.75f;
            [surface setAccelerationX:0 y:0 z:-1];
            printf("S5LBOX-HLE-MOTION-BRIDGE %s\n",motionOK?"PASS":"FAIL");
            int status=s5lbox_hle_run_presented(&config,NULL,&presentation,control,&result);
            [surface finish];
            printf("S5LBOX-HLE-VISUAL END status=%d frames=%lu message=%s\n",status,(unsigned long)surface.frameCount,result.message);
            NSString *message=[NSString stringWithUTF8String:result.message]?:@"Runtime failed";
            dispatch_async(dispatch_get_main_queue(), ^{
                s5lbox_hle_control_release(control); self.control=NULL;
                self.status.text=[NSString stringWithFormat:@"Graphics ended: %d, %lu frames\n%@",status,(unsigned long)surface.frameCount,message];
                [self.button setTitle:@"Run graphics test again" forState:UIControlStateNormal];
            });
        }
    });
}
- (void)frame {
    NSData *data=[self.surface takeFrame]; if (!data) return;
    const uint8_t *sample=(const uint8_t *)data.bytes+(10*320+10)*4;
    unsigned matchedColor=(abs((int)sample[0]-64)<4 && abs((int)sample[1]-31)<4 && abs((int)sample[2]-15)<4)?1:
        (abs((int)sample[0]-56)<4 && abs((int)sample[1]-82)<4 && abs((int)sample[2]-13)<4)?2:0;
    if (matchedColor && !(self.verifiedColors&matchedColor)) {
        self.verifiedColors|=matchedColor;
        printf("S5LBOX-HLE-VISUAL PIXELS %s PASS\n",matchedColor==1?"initial":"touch");
    }
    void *retained=(__bridge_retained void *)data;
    CGDataProviderRef provider=CGDataProviderCreateWithData(retained,data.bytes,data.length,ReleaseFrame);
    if (!provider) { CFRelease(retained); return; }
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(self.surface.width,self.surface.height,8,32,self.surface.width*4,color,
        kCGBitmapByteOrder32Little|kCGImageAlphaNoneSkipFirst,provider,NULL,false,kCGRenderingIntentDefault);
    self.display.layer.contents=(__bridge id)image;
    CGImageRelease(image); CGColorSpaceRelease(color); CGDataProviderRelease(provider);
    if (self.control) self.status.text=[NSString stringWithFormat:@"ARMv6 UIKit in S5LBox\n%lu frames",(unsigned long)self.surface.frameCount];
}
@end
void s5lbox_start_graphics(UIViewController *parent) {
    HLEGraphicsProbe *controller=[HLEGraphicsProbe new]; controller.modalPresentationStyle=UIModalPresentationFullScreen;
    [parent presentViewController:controller animated:NO completion:nil];
}
