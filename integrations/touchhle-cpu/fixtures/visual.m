/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Tiny ARMv6 UIKit app. Public API declarations are kept here so the community
 * SDK can build the test without distributing Apple headers or firmware. */
#include <stdio.h>
#include <stdlib.h>
typedef signed char BOOL;
typedef struct { float x,y; } CGPoint;
typedef struct { float width,height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;
__attribute__((objc_root_class)) @interface NSObject { Class isa; }
+ (id)alloc;
- (id)init;
@end
@interface NSAutoreleasePool : NSObject @end
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)text;
@end
@interface UIColor : NSObject
+ (id)colorWithRed:(float)red green:(float)green blue:(float)blue alpha:(float)alpha;
+ (id)whiteColor;
@end
@interface UIView : NSObject
- (id)initWithFrame:(CGRect)rect;
- (void)setFrame:(CGRect)rect;
- (void)setBackgroundColor:(id)color;
- (void)addSubview:(id)view;
@end
@interface UIWindow : UIView
- (void)makeKeyAndVisible;
@end
@interface UILabel : UIView
- (void)setText:(id)text;
- (void)setTextColor:(id)color;
- (void)setTextAlignment:(int)alignment;
@end
@interface UIButton : UIView
+ (id)buttonWithType:(int)type;
- (void)setTitle:(id)title forState:(unsigned)state;
- (void)addTarget:(id)target action:(SEL)action forControlEvents:(unsigned)events;
@end
@interface UIApplication : NSObject
- (void)setStatusBarHidden:(BOOL)hidden;
@end
extern int UIApplicationMain(int argc,char **argv,id principal,id delegate);
@interface HLEVisualDelegate : NSObject {
    UIWindow *_window;
    UILabel *_label;
    unsigned _taps;
}
@end
@implementation HLEVisualDelegate
- (void)applicationDidFinishLaunching:(UIApplication *)application {
    [application setStatusBarHidden:1];
    _window=[[UIWindow alloc] initWithFrame:(CGRect){{0,0},{320,480}}];
    [_window setBackgroundColor:[UIColor colorWithRed:0.06f green:0.12f blue:0.25f alpha:1]];
    _label=[[UILabel alloc] initWithFrame:(CGRect){{12,130},{296,60}}];
    [_label setText:@"ARMv6 UIKit is running"];
    [_label setTextColor:[UIColor whiteColor]];
    [_label setTextAlignment:1];
    [_window addSubview:_label];
    UIButton *button=[UIButton buttonWithType:1];
    [button setFrame:(CGRect){{50,245},{220,64}}];
    [button setTitle:@"Test guest touch" forState:0];
    [button addTarget:self action:@selector(tapped:) forControlEvents:64];
    [_window addSubview:button];
    [_window makeKeyAndVisible];
    puts("S5LBOX-HLE-VISUAL READY"); fflush(stdout);
}
- (void)tapped:(id)sender {
    (void)sender;
    char text[96]; snprintf(text,sizeof text,"Guest touches: %u",++_taps);
    [_label setText:[NSString stringWithUTF8String:text]];
    [_window setBackgroundColor:[UIColor colorWithRed:0.05f green:0.32f blue:0.22f alpha:1]];
    char path[1024]; snprintf(path,sizeof path,"%s/Documents/visual-touch.txt",getenv("HOME"));
    FILE *file=fopen(path,"w");
    if (file) { fprintf(file,"%u\n",_taps); fclose(file); }
    printf("S5LBOX-HLE-VISUAL TOUCH count=%u saved=%d\n",_taps,file!=NULL); fflush(stdout);
}
@end
int main(int argc,char **argv) {
    (void)[[NSAutoreleasePool alloc] init];
    return UIApplicationMain(argc,argv,0,@"HLEVisualDelegate");
}
