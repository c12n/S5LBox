// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
// Kept out of the normal app until the integrated feature passes device tests.
#if S5LBOX_TOUCHHLE
#import "VMTouchHLEViewController.h"
#import "VMTouchHLESurface.h"
#import "VMTouchHLEFiles.h"
#import "VMGuestApplications.h"
#import "VMPackageBridge.h"
#import "VMUSBService.h"
#include <math.h>

@interface HLEDisplayView : UIView <UIKeyInput>
@property(nonatomic,weak) VMTouchHLESurface *surface;
@property(nonatomic) BOOL inputEnabled;
- (void)showFrame:(NSData *)data;
@end
static void HLEReleaseFrame(void *context,const void *bytes,size_t count) {
    (void)bytes; (void)count; CFRelease(context);
}
@implementation HLEDisplayView {
    NSMutableDictionary<NSNumber *,NSValue *> *_pressed;
}
@synthesize inputEnabled=_inputEnabled;
- (void)setInputEnabled:(BOOL)enabled {
    if (_inputEnabled==enabled) return;
    if (!enabled) {
        // Finish fingers already delivered to the guest. New taps during a
        // pause must not become a delayed burst of input after Resume.
        for (NSNumber *finger in _pressed) {
            CGPoint point=_pressed[finger].CGPointValue;
            s5lbox_hle_input_event event={0}; event.kind=9;
            event.finger=finger.longLongValue; event.x=point.x; event.y=point.y;
            [self.surface enqueueInput:event];
        }
        [_pressed removeAllObjects];
    }
    _inputEnabled=enabled;
    self.userInteractionEnabled=enabled;
}
- (void)showFrame:(NSData *)data {
    uint32_t width=self.surface.width,height=self.surface.height;
    if (!data || !width || !height || data.length!=(NSUInteger)width*height*4) return;
    void *retained=(__bridge_retained void *)data;
    CGDataProviderRef provider=CGDataProviderCreateWithData(retained,data.bytes,data.length,HLEReleaseFrame);
    if (!provider) { CFRelease(retained); return; }
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGImageRef frame=CGImageCreate(width,height,8,32,width*4,color,
        kCGBitmapByteOrder32Little|kCGImageAlphaNoneSkipFirst,provider,NULL,false,kCGRenderingIntentDefault);
    self.layer.contents=(__bridge id)frame;
    if (frame) CGImageRelease(frame);
    CGColorSpaceRelease(color); CGDataProviderRelease(provider);
}
- (void)touches:(NSSet<UITouch *> *)touches kind:(uint32_t)kind {
    if (!_inputEnabled || self.bounds.size.width<=0 || self.bounds.size.height<=0) return;
    for (UITouch *touch in touches) {
        CGPoint point=[touch locationInView:self];
        s5lbox_hle_input_event event={0}; event.kind=kind;
        event.finger=(int64_t)(uintptr_t)(__bridge void *)touch;
        event.x=fmaxf(0,fminf(1,(float)(point.x/self.bounds.size.width)));
        event.y=fmaxf(0,fminf(1,(float)(point.y/self.bounds.size.height)));
        if ([self.surface enqueueInput:event]) {
            if (!_pressed) _pressed=[NSMutableDictionary dictionary];
            NSNumber *finger=@(event.finger);
            if (kind==3 || kind==9) [_pressed removeObjectForKey:finger];
            else _pressed[finger]=[NSValue valueWithCGPoint:CGPointMake(event.x,event.y)];
        }
    }
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:1]; }
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:2]; }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:3]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self touches:touches kind:9]; }
- (BOOL)canBecomeFirstResponder { return _inputEnabled; }
- (BOOL)hasText { return YES; }
- (void)deleteBackward { if (!_inputEnabled) return; s5lbox_hle_input_event event={0}; event.kind=5; [self.surface enqueueInput:event]; }
- (void)insertText:(NSString *)text {
    if (!_inputEnabled) return;
    if ([text isEqual:@"\n"]) { s5lbox_hle_input_event event={0}; event.kind=6; [self.surface enqueueInput:event]; return; }
    NSData *bytes=[text dataUsingEncoding:NSUTF8StringEncoding]; const uint8_t *p=bytes.bytes; NSUInteger left=bytes.length;
    while (left) {
        NSUInteger count=MIN(left,31u);
        // Split only at UTF-8 character boundaries, preserving pasted text.
        if (count<left) while (count && (p[count]&0xc0)==0x80) count--;
        if (!count) return;
        s5lbox_hle_input_event event={0}; event.kind=4; memcpy(event.text,p,count);
        if (![self.surface enqueueInput:event]) return;
        p+=count; left-=count;
    }
}
@end

@interface HLERunViewController : UIViewController
@property(nonatomic,strong) VMGuestApplication *application;
@property(nonatomic,strong) VMUSBTransport *transport;
@property(nonatomic,copy) NSString *instanceID;
@end
@implementation HLERunViewController {
    HLEDisplayView *_display;
    UILabel *_status;
    UIButton *_home,*_controls;
    UIActivityIndicatorView *_spinner;
    CADisplayLink *_link;
    VMTouchHLESurface *_surface;
    VMPackageBridge *_bridge;
    const s5lbox_hle_control *_control;
    BOOL _paused,_closing,_forceAvailable,_finished,_inactive,_wantsTextInput;
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor=UIColor.systemBackgroundColor;
    self.title=self.application.name; self.modalInPresentation=YES;
    self.navigationController.modalInPresentation=YES;
    _display=[HLEDisplayView new]; _display.backgroundColor=UIColor.blackColor;
    _display.multipleTouchEnabled=YES; _display.layer.magnificationFilter=kCAFilterNearest;
    _display.accessibilityLabel=self.application.name;
    _status=[UILabel new]; _status.font=[UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    _status.adjustsFontForContentSizeCategory=YES; _status.numberOfLines=0;
    _status.textAlignment=NSTextAlignmentCenter; _status.textColor=UIColor.whiteColor; _status.text=@"Opening app…";
    _spinner=[[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    _spinner.color=UIColor.whiteColor;
    [_spinner startAnimating];
    _home=[UIButton buttonWithType:UIButtonTypeSystem]; [_home setTitle:@"Home" forState:UIControlStateNormal];
    [_home addTarget:self action:@selector(home) forControlEvents:UIControlEventTouchUpInside];
    _controls=[UIButton buttonWithType:UIButtonTypeSystem]; [_controls setImage:[UIImage systemImageNamed:@"slider.horizontal.3"] forState:UIControlStateNormal];
    _controls.accessibilityLabel=@"App controls";
    [_controls addTarget:self action:@selector(controls) forControlEvents:UIControlEventTouchUpInside];
    for (UIView *view in @[_display,_status,_spinner,_home,_controls]) [self.view addSubview:view];
    _surface=[[VMTouchHLESurface alloc] initWithWidth:320 height:480]; _display.surface=_surface;
    __weak HLERunViewController *weakSelf=self;
    _surface.textInputChanged=^(BOOL enabled) {
        HLERunViewController *owner=weakSelf;
        if (!owner) return;
        owner->_wantsTextInput=enabled;
        [owner updateInputState];
    };
    _link=[CADisplayLink displayLinkWithTarget:self selector:@selector(frame)];
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(inactive) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(active) name:UIApplicationDidBecomeActiveNotification object:nil];
    _inactive=UIApplication.sharedApplication.applicationState!=UIApplicationStateActive;
    [self updateInputState];
    [self start];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews]; CGRect safe=self.view.safeAreaLayoutGuide.layoutFrame;
    CGFloat height=MAX(0,MIN(safe.size.height-84,(safe.size.width-32)*1.5)),width=height/1.5;
    _display.frame=CGRectMake(CGRectGetMidX(safe)-width/2,safe.origin.y+(safe.size.height-68-height)/2,width,height);
    _status.frame=CGRectInset(_display.frame,16,16); _spinner.center=CGPointMake(CGRectGetMidX(safe),CGRectGetMinY(_display.frame)+32);
    _home.frame=CGRectMake(CGRectGetMidX(safe)-54,CGRectGetMaxY(safe)-60,108,48);
    _controls.frame=CGRectMake(CGRectGetMaxX(safe)-72,CGRectGetMaxY(safe)-60,48,48);
}
- (void)start {
    _control=s5lbox_hle_control_create();
    s5lbox_hle_control_pause(_control,_paused||_inactive);
    _bridge=[[VMPackageBridge alloc] initWithTransport:self.transport instanceID:self.instanceID];
    VMPackageBridge *bridge=_bridge; VMTouchHLESurface *surface=_surface;
    const s5lbox_hle_control *control=_control; VMGuestApplication *app=self.application;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            NSError *error=nil; VMTouchHLEFiles *files=nil;
            VMUSBService *service=[bridge openLiveFilesForContainer:app.containerID error:&error];
            s5lbox_hle_outcome outcome={0}; int result=S5LBOX_HLE_ERROR;
            if (service) {
                files=[[VMTouchHLEFiles alloc] initWithService:service homeDirectory:app.homeDirectory bundleDirectory:app.bundleDirectory];
                NSURL *support=[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
                NSURL *data=[[support URLByAppendingPathComponent:@"touchHLE"] URLByAppendingPathComponent:self.instanceID];
                if ([NSFileManager.defaultManager createDirectoryAtURL:data withIntermediateDirectories:YES attributes:nil error:&error]) {
                    NSString *resources=NSBundle.mainBundle.resourcePath;
                    s5lbox_hle_config config={0}; config.abi_version=1;
                    config.resource_directory=resources.fileSystemRepresentation; config.data_directory=data.fileSystemRepresentation;
                    s5lbox_hle_live_config live=files.configuration;
                    s5lbox_hle_presentation presentation=surface.configuration;
                    result=s5lbox_hle_run_presented(&config,&live,&presentation,control,&outcome);
                    if (files.error) error=files.error;
                }
            }
            [surface finish]; [files close]; [service close];
            NSString *message=error.localizedDescription ?: [NSString stringWithUTF8String:outcome.message] ?: @"The app could not run.";
            dispatch_async(dispatch_get_main_queue(), ^{
                s5lbox_hle_control_release(control); self->_control=NULL; self->_finished=YES;
                [self->_link invalidate]; self->_link=nil; [self->_spinner stopAnimating];
                [self updateInputState];
                if (self->_closing || (result==S5LBOX_HLE_OK && !error)) [self dismissViewControllerAnimated:YES completion:nil];
                else { self->_status.text=message; self->_status.hidden=NO; self->_status.textColor=UIColor.labelColor; self->_display.hidden=YES; }
            });
        }
    });
}
- (void)frame {
    NSData *frame=[_surface takeFrame]; if (!frame) return;
    [_display showFrame:frame]; if (!_closing && !_paused) _status.hidden=YES; [_spinner stopAnimating];
}
- (void)updateInputState {
    _display.inputEnabled=!(_paused||_inactive||_closing||_finished);
    if (_display.inputEnabled && _wantsTextInput) [_display becomeFirstResponder];
    else [_display resignFirstResponder];
    _link.paused=_paused||_inactive;
    if (!_closing && !_finished) {
        _status.text=_paused?@"Paused":@"Opening app…";
        _status.hidden=!_paused && _surface.frameCount>0;
    }
}
- (void)inactive {
    _inactive=YES; [self updateInputState];
    if (_control) s5lbox_hle_control_pause(_control,true);
}
- (void)active {
    _inactive=NO; [self updateInputState];
    if (_control) s5lbox_hle_control_pause(_control,_paused);
}
- (void)home {
    if (_finished) { [self dismissViewControllerAnimated:YES completion:nil]; return; }
    if (_forceAvailable) { if (_control) s5lbox_hle_control_stop(_control); _bridge.canceled=YES; return; }
    if (_closing) return;
    _closing=YES; _paused=NO; if (_control) s5lbox_hle_control_pause(_control,false);
    [self updateInputState];
    s5lbox_hle_input_event event={0}; event.kind=8; [_surface enqueueInput:event];
    _status.text=@"Closing app…"; _status.textColor=UIColor.whiteColor; _status.hidden=NO;
    [_display resignFirstResponder];
    __weak HLERunViewController *weakSelf=self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(), ^{
        HLERunViewController *owner=weakSelf; if (!owner || owner->_finished) return;
        owner->_forceAvailable=YES; [owner->_home setTitle:@"Force Close" forState:UIControlStateNormal];
        owner->_status.text=@"The app has not closed. Force Close may lose unsaved changes.";
    });
}
- (void)controls {
    if (_closing || _finished) return;
    UIAlertController *sheet=[UIAlertController alertControllerWithTitle:nil message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    [sheet addAction:[UIAlertAction actionWithTitle:_paused?@"Resume":@"Pause" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *action) {
        self->_paused=!self->_paused; [self updateInputState];
        if (self->_control) s5lbox_hle_control_pause(self->_control,self->_paused||self->_inactive);
    }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    sheet.popoverPresentationController.sourceView=_controls; sheet.popoverPresentationController.sourceRect=_controls.bounds;
    [self presentViewController:sheet animated:YES completion:nil];
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; [_link invalidate]; }
@end

@implementation VMTouchHLEAppsViewController {
    VMUSBTransport *_transport;
    NSString *_instanceID;
    VMGuestApplications *_discovery;
    VMPackageBridge *_setup;
    NSArray<VMGuestApplication *> *_apps;
    BOOL _busy;
    UILabel *_message;
}
- (instancetype)initWithTransport:(VMUSBTransport *)transport instanceID:(NSString *)identifier {
    self=[super initWithStyle:UITableViewStyleInsetGrouped];
    if (self) { _transport=transport; _instanceID=[identifier copy]; }
    return self;
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.title=@"touchHLE";
    self.navigationItem.largeTitleDisplayMode=UINavigationItemLargeTitleDisplayModeNever;
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Set Up" style:UIBarButtonItemStylePlain target:self action:@selector(setup)];
    self.refreshControl=[UIRefreshControl new]; [self.refreshControl addTarget:self action:@selector(refresh) forControlEvents:UIControlEventValueChanged];
    self.tableView.rowHeight=UITableViewAutomaticDimension; self.tableView.estimatedRowHeight=60;
    _message=[UILabel new]; _message.textAlignment=NSTextAlignmentCenter; _message.numberOfLines=0;
    _message.font=[UIFont preferredFontForTextStyle:UIFontTextStyleBody]; _message.adjustsFontForContentSizeCategory=YES;
    [self refresh];
}
- (void)message:(NSString *)text {
    _message.text=text; self.tableView.backgroundView=text.length?_message:nil;
}
- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    if (self.isMovingFromParentViewController || self.navigationController.isBeingDismissed)
        _discovery.canceled=YES;
}
- (void)refresh {
    if (_busy) return; _busy=YES; [self message:_apps.count?nil:@"Loading installed apps…"];
    _discovery=[[VMGuestApplications alloc] initWithTransport:_transport instanceID:_instanceID];
    VMGuestApplications *discovery=_discovery;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSError *error=nil; NSArray *apps=[discovery installedApplications:&error];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_busy=NO; [self.refreshControl endRefreshing];
            self->_apps=apps?:@[];
            [self.tableView reloadData];
            [self message:error.localizedDescription ?: (apps.count?nil:@"Install an app in this guest to open it here.")];
        });
    });
}
- (NSInteger)tableView:(UITableView *)table numberOfRowsInSection:(NSInteger)section { (void)table; (void)section; return _apps.count; }
- (UITableViewCell *)tableView:(UITableView *)table cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell=[table dequeueReusableCellWithIdentifier:@"app"];
    if (!cell) cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"app"];
    VMGuestApplication *app=_apps[path.row]; cell.textLabel.text=app.name; cell.detailTextLabel.text=app.version;
    cell.textLabel.font=[UIFont preferredFontForTextStyle:UIFontTextStyleBody]; cell.textLabel.adjustsFontForContentSizeCategory=YES;
    cell.detailTextLabel.font=[UIFont preferredFontForTextStyle:UIFontTextStyleCaption1]; cell.detailTextLabel.adjustsFontForContentSizeCategory=YES;
    cell.accessoryType=UITableViewCellAccessoryDisclosureIndicator; return cell;
}
- (void)tableView:(UITableView *)table didSelectRowAtIndexPath:(NSIndexPath *)path {
    [table deselectRowAtIndexPath:path animated:YES]; if (_busy) return;
    UIViewController *presenter=self.navigationController.presentingViewController; if (!presenter) return;
    HLERunViewController *run=[HLERunViewController new]; run.application=_apps[path.row];
    run.transport=_transport; run.instanceID=_instanceID;
    UINavigationController *nav=[[UINavigationController alloc] initWithRootViewController:run]; nav.modalPresentationStyle=UIModalPresentationFullScreen;
    [presenter dismissViewControllerAnimated:YES completion:^{ [presenter presentViewController:nav animated:YES completion:nil]; }];
}
- (void)setup {
    if (_busy) return;
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"Set up touchHLE?"
        message:@"Install guest support and respring iPhone OS. Close guest apps first."
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Set Up" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *action) { [self installSupport]; }]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (void)installSupport {
    if (_busy) return;
    _apps=@[]; [self.tableView reloadData];
    _busy=YES; self.navigationItem.hidesBackButton=YES; self.navigationController.modalInPresentation=YES;
    self.navigationItem.rightBarButtonItem.enabled=NO; [self message:@"Setting up touchHLE…"];
    _setup=[[VMPackageBridge alloc] initWithTransport:_transport instanceID:_instanceID];
    VMPackageBridge *setup=_setup;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSError *error=nil; NSData *status=[setup status:&error];
        NSURL *package=[NSBundle.mainBundle URLForResource:@"s5lbox-hle-launch" withExtension:@"deb"];
        if (status && package) status=[setup install:@[package] status:status error:&error];
        else if (status) error=[NSError errorWithDomain:@"S5LBox.HLE" code:1 userInfo:@{NSLocalizedDescriptionKey:@"The guest launch package is missing from this build."}];
        BOOL okay=status && !error;
        if (okay && setup.finishAction) okay=[setup performFinish:status error:&error];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_busy=NO; self.navigationItem.hidesBackButton=NO; self.navigationController.modalInPresentation=NO;
            self.navigationItem.rightBarButtonItem.enabled=YES;
            [self message:okay?@"Guest setup completed. Wait for the respring, then refresh.":error.localizedDescription];
        });
    });
}
- (void)dealloc { _discovery.canceled=YES; _setup.canceled=YES; }
@end
#endif
