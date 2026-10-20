// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
// Drawing and destruction run on one runtime worker. A gate excludes that
// worker while the main thread drains contexts for an inactive transition.
// The main thread consumes immutable frames; no EAGL layer crosses threads.
#import "VMTouchHLESurface.h"
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES1/gl.h>
#import <OpenGLES/ES1/glext.h>
#include <dlfcn.h>

@interface HLEGLContext : NSObject
@property(nonatomic,strong) EAGLContext *context;
@property(nonatomic) GLuint framebuffer,color;
@end
@implementation HLEGLContext
@end
static _Thread_local void *CurrentContext;
static _Thread_local void *GraphicsOwner;
static _Thread_local NSUInteger GraphicsDepth;

@interface VMTouchHLESurface ()
- (void *)createContext:(BOOL)shared;
- (void)destroyContext:(void *)handle;
- (int)presentFrame;
- (int)pollEvent:(s5lbox_hle_input_event *)event;
- (void)enterGraphics;
- (void)leaveGraphics;
@end
static HLEGLContext *Current(void) { return (__bridge HLEGLContext *)CurrentContext; }
static void BindFramebuffer(GLenum target,GLuint framebuffer) {
    glBindFramebufferOES(target,framebuffer?:Current().framebuffer);
}
static void GetInteger(GLenum name,GLint *value) {
    glGetIntegerv(name,value);
    if (name==GL_FRAMEBUFFER_BINDING_OES && (GLuint)*value==Current().framebuffer) *value=0;
}
static void *Create(void *owner,bool shared) {
    VMTouchHLESurface *surface=(__bridge VMTouchHLESurface *)owner;
    [surface enterGraphics];
    @autoreleasepool { @try { return [surface createContext:shared]; }
        @catch (__unused NSException *error) { return NULL; }
        @finally { [surface leaveGraphics]; } }
}
static void Destroy(void *owner,void *handle) {
    VMTouchHLESurface *surface=(__bridge VMTouchHLESurface *)owner;
    [surface enterGraphics];
    @autoreleasepool { @try { [surface destroyContext:handle]; }
        @catch (__unused NSException *error) {}
        @finally { [surface leaveGraphics]; } }
}
static int32_t MakeCurrent(void *owner,void *handle) {
    VMTouchHLESurface *surface=(__bridge VMTouchHLESurface *)owner;
    [surface enterGraphics];
    @autoreleasepool { @try {
        HLEGLContext *context=(__bridge HLEGLContext *)handle;
        if (CurrentContext!=handle) {
            if (CurrentContext) glFlush();
            if (![EAGLContext setCurrentContext:context.context]) return -1;
            CurrentContext=handle;
        }
        return 0;
    } @catch (__unused NSException *error) { return -1; }
      @finally { [surface leaveGraphics]; } }
}
static bool IsCurrent(void *owner,void *handle) { (void)owner; return handle && CurrentContext==handle; }
static const void *GetProc(void *owner,const char *name) {
    (void)owner;
    if (!strcmp(name,"glBindFramebufferOES")) return (const void *)BindFramebuffer;
    if (!strcmp(name,"glGetIntegerv")) return (const void *)GetInteger;
    static void *library; static dispatch_once_t once;
    dispatch_once(&once, ^{ library=dlopen("/System/Library/Frameworks/OpenGLES.framework/OpenGLES",RTLD_LAZY|RTLD_LOCAL); });
    return library?dlsym(library,name):NULL;
}
static int32_t Present(void *owner) {
    VMTouchHLESurface *surface=(__bridge VMTouchHLESurface *)owner;
    [surface enterGraphics];
    @autoreleasepool { @try { return [surface presentFrame]; }
        @catch (__unused NSException *error) { return -1; }
        @finally { [surface leaveGraphics]; } }
}
static void EnterGraphics(void *owner) { [(__bridge VMTouchHLESurface *)owner enterGraphics]; }
static void LeaveGraphics(void *owner) { [(__bridge VMTouchHLESurface *)owner leaveGraphics]; }
static int32_t Poll(void *owner,s5lbox_hle_input_event *event) {
    @autoreleasepool { @try { return [(__bridge VMTouchHLESurface *)owner pollEvent:event]; }
        @catch (__unused NSException *error) { return -1; } }
}
static void TextInput(void *owner,bool enabled) {
    VMTouchHLESurface *surface=(__bridge VMTouchHLESurface *)owner;
    dispatch_async(dispatch_get_main_queue(), ^{ if (surface.textInputChanged) surface.textInputChanged(enabled); });
}
static void IdleTimer(void *owner,bool enabled) {
    VMTouchHLESurface *surface=(__bridge VMTouchHLESurface *)owner;
    dispatch_async(dispatch_get_main_queue(), ^{ if (surface.idleTimerChanged) surface.idleTimerChanged(enabled); });
}
@implementation VMTouchHLESurface {
    NSMutableSet<HLEGLContext *> *_contexts;
    NSMutableData *_scratch;
    NSData *_latest;
    NSUInteger _frameCount;
    s5lbox_hle_input_event _events[256];
    NSUInteger _head,_count;
    BOOL _overflow;
    NSCondition *_graphicsGate;
    BOOL _graphicsSuspended,_graphicsBusy;
}
- (instancetype)initWithWidth:(uint32_t)width height:(uint32_t)height {
    if (!width || !height || width>4096 || height>4096) return nil;
    self=[super init];
    if (self) { _width=width; _height=height; _contexts=[NSMutableSet set]; _graphicsGate=[NSCondition new]; }
    return self;
}
- (s5lbox_hle_presentation)configuration {
    s5lbox_hle_presentation surface={0};
    surface.abi_version=2; surface.width=_width; surface.height=_height; surface.context=(__bridge void *)self;
    surface.create_gl=Create; surface.destroy_gl=Destroy; surface.make_current=MakeCurrent;
    surface.is_current=IsCurrent; surface.get_proc=GetProc; surface.present=Present; surface.poll_event=Poll;
    surface.text_input=TextInput; surface.idle_timer=IdleTimer;
    surface.graphics_enter=EnterGraphics; surface.graphics_leave=LeaveGraphics;
    return surface;
}
- (void)enterGraphics {
    // Context callbacks can nest inside a Rust GLES borrow. They must not wait
    // for suspension while the outer borrow is preventing that suspension.
    if (GraphicsOwner==(__bridge void *)self) { GraphicsDepth++; return; }
    [_graphicsGate lock];
    while (_graphicsSuspended || _graphicsBusy) [_graphicsGate wait];
    _graphicsBusy=YES;
    GraphicsOwner=(__bridge void *)self; GraphicsDepth=1;
    [_graphicsGate unlock];
}
- (void)leaveGraphics {
    if (--GraphicsDepth) return;
    GraphicsOwner=NULL;
    [_graphicsGate lock]; _graphicsBusy=NO; [_graphicsGate broadcast]; [_graphicsGate unlock];
}
- (void)setGraphicsSuspended:(BOOL)suspended {
    [_graphicsGate lock];
    if (_graphicsSuspended==suspended) { [_graphicsGate unlock]; return; }
    _graphicsSuspended=suspended;
    if (suspended) {
        while (_graphicsBusy) [_graphicsGate wait];
        // No raw GLES borrow or context callback can overlap this drain.
        // EAGL permits serialized access across threads; restore this thread's
        // context without modifying the worker's thread-local current context.
        EAGLContext *previous=EAGLContext.currentContext;
        for (HLEGLContext *item in _contexts) {
            if ([EAGLContext setCurrentContext:item.context]) glFinish();
        }
        [EAGLContext setCurrentContext:previous];
    } else [_graphicsGate broadcast];
    [_graphicsGate unlock];
}
- (void *)createContext:(BOOL)shared {
    HLEGLContext *item=[HLEGLContext new];
    EAGLSharegroup *group=shared?Current().context.sharegroup:nil;
    if (shared && !group) return NULL;
    item.context=group?[[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES1 sharegroup:group]:
        [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES1];
    if (!item.context || MakeCurrent((__bridge void *)self,(__bridge void *)item)) return NULL;
    GLuint framebuffer=0,color=0;
    glGenFramebuffersOES(1,&framebuffer); glBindFramebufferOES(GL_FRAMEBUFFER_OES,framebuffer);
    glGenRenderbuffersOES(1,&color); glBindRenderbufferOES(GL_RENDERBUFFER_OES,color);
    glRenderbufferStorageOES(GL_RENDERBUFFER_OES,GL_RGBA8_OES,_width,_height);
    glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES,GL_COLOR_ATTACHMENT0_OES,GL_RENDERBUFFER_OES,color);
    item.framebuffer=framebuffer; item.color=color;
    if (glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES)!=GL_FRAMEBUFFER_COMPLETE_OES) {
        [self destroyContext:(__bridge void *)item]; return NULL;
    }
    glViewport(0,0,_width,_height); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    [_contexts addObject:item];
    return (__bridge void *)item;
}
- (void)destroyContext:(void *)handle {
    if (!handle) return;
    HLEGLContext *item=(__bridge HLEGLContext *)handle, *previous=Current();
    if (MakeCurrent((__bridge void *)self,handle)==0) {
        GLuint framebuffer=item.framebuffer,color=item.color;
        if (framebuffer) glDeleteFramebuffersOES(1,&framebuffer);
        if (color) glDeleteRenderbuffersOES(1,&color);
        // This context leaves the drain set below. Finish its pending work
        // first, including deletion, even when a background transition races.
        glFinish();
    }
    [EAGLContext setCurrentContext:nil]; CurrentContext=NULL;
    [_contexts removeObject:item];
    if (previous && previous!=item) MakeCurrent((__bridge void *)self,(__bridge void *)previous);
}
- (int)presentFrame {
    HLEGLContext *context=Current();
    if (!context || ![_contexts containsObject:context]) return -1;
    NSUInteger size=(NSUInteger)_width*_height*4;
    if (!_scratch) _scratch=[NSMutableData dataWithLength:size];
    GLint previous,pack;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES,&previous); glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
    glBindFramebufferOES(GL_FRAMEBUFFER_OES,context.framebuffer); glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,_width,_height,GL_RGBA,GL_UNSIGNED_BYTE,_scratch.mutableBytes);
    glPixelStorei(GL_PACK_ALIGNMENT,pack); glBindFramebufferOES(GL_FRAMEBUFFER_OES,(GLuint)previous);
    NSMutableData *frame=[NSMutableData dataWithLength:size];
    const uint8_t *source=_scratch.bytes; uint8_t *target=frame.mutableBytes;
    for (uint32_t y=0;y<_height;y++) for (uint32_t x=0;x<_width;x++) {
        const uint8_t *p=source+((NSUInteger)(_height-y-1)*_width+x)*4;
        uint8_t *q=target+((NSUInteger)y*_width+x)*4;
        q[0]=p[2]; q[1]=p[1]; q[2]=p[0]; q[3]=255;
    }
    @synchronized(self) { _latest=frame; _frameCount++; }
    return 0;
}
- (NSData *)takeFrame { @synchronized(self) { NSData *result=_latest; _latest=nil; return result; } }
- (NSUInteger)frameCount { @synchronized(self) { return _frameCount; } }
- (BOOL)enqueueInput:(s5lbox_hle_input_event)event {
    @synchronized(self) {
        // Coalesce adjacent motion for the same finger; never discard an up.
        if (event.kind==2 && _count) {
            NSUInteger last=(_head+_count-1)%256;
            if (_events[last].kind==2 && _events[last].finger==event.finger) { _events[last]=event; return YES; }
        }
        if (_count==256) { _overflow=YES; return NO; }
        _events[(_head+_count++)%256]=event; return YES;
    }
}
- (int)pollEvent:(s5lbox_hle_input_event *)event {
    @synchronized(self) {
        if (_overflow) return -1;
        if (!_count) return 0;
        *event=_events[_head]; _head=(_head+1)%256; _count--; return 1;
    }
}
- (void)finish {
    [self enterGraphics];
    @try {
        for (HLEGLContext *context in _contexts.allObjects) [self destroyContext:(__bridge void *)context];
        _scratch=nil;
    } @finally { [self leaveGraphics]; }
}
@end
