// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
//! Embedded window, never initializes SDL or owns the host application's loop.
use crate::gles::{create_gles1_ctx_no_parent_stack, GLESContext, GLES};
use crate::image::Image;
use crate::matrix::Matrix;
use crate::options::Options;
use crate::presentation::{InputEvent, Presentation};
pub use crate::window_types::*;
use crate::Environment;
use std::cell::Cell;
use std::collections::{HashMap, VecDeque};
use std::f32::consts::{FRAC_PI_2, PI};
use std::ffi::{c_void, CString};

pub struct GLContext {
    surface: Presentation,
    handle: *mut c_void,
}
impl GLContext {
    pub fn graphics_access(&self) -> crate::presentation::GraphicsAccess {
        crate::presentation::GraphicsAccess::new(self.surface)
    }
    pub fn is_current(&self) -> bool {
        unsafe { (self.surface.is_current.unwrap())(self.surface.context, self.handle) }
    }
}
impl Drop for GLContext {
    fn drop(&mut self) {
        unsafe {
            (self.surface.destroy_gl.unwrap())(self.surface.context, self.handle);
        }
    }
}
fn make_current(surface: Presentation, context: &GLContext) {
    assert_eq!(
        unsafe { (surface.make_current.unwrap())(surface.context, context.handle) },
        0,
        "Native graphics context became unavailable"
    );
}
fn get_proc(surface: Presentation, name: &str) -> *const c_void {
    let name = CString::new(name).unwrap();
    unsafe { (surface.get_proc.unwrap())(surface.context, name.as_ptr()) }
}

pub struct Window {
    surface: Presentation,
    internal: Option<Box<dyn GLESContext>>,
    family: DeviceFamily,
    orientation: DeviceOrientation,
    share_current: Cell<bool>,
    screen_saver: bool,
    events: VecDeque<Event>,
    pub(crate) on_main_stack: bool,
}
impl Window {
    pub fn rotatable_fullscreen() -> bool {
        false
    }
    pub fn new(
        _title: &str,
        _icon: Option<Image>,
        launch: Option<Image>,
        options: &Options,
    ) -> Self {
        let surface = crate::host::presentation().expect("Missing native presentation callbacks");
        let mut window = Self {
            surface,
            internal: None,
            family: options.device_family.unwrap_or(DeviceFamily::iPhone),
            orientation: options.initial_orientation,
            share_current: Cell::new(false),
            screen_saver: true,
            events: VecDeque::new(),
            on_main_stack: true,
        };
        let mut graphics = create_gles1_ctx_no_parent_stack(&mut window, options);
        {
            let context = graphics.make_current(&mut window);
            log!("Native graphics: {}", unsafe {
                context.driver_description()
            });
        }
        window.internal = Some(graphics);
        if let Some(image) = launch {
            window.show_launch_image(&image);
        }
        window
    }
    fn show_launch_image(&mut self, image: &Image) {
        use crate::gles::gles11_raw as gl;
        let viewport = self.viewport();
        let matrix = self.rotation_matrix().multiply(&Matrix::y_flip());
        unsafe {
            let mut graphics = self.make_internal_gl_ctx_current();
            let mut texture = 0;
            graphics.GenTextures(1, &mut texture);
            graphics.BindTexture(gl::TEXTURE_2D, texture);
            crate::gles::present::configure_frame_texture(graphics.as_mut());
            let (width, height) = image.dimensions();
            graphics.TexImage2D(
                gl::TEXTURE_2D,
                0,
                gl::RGBA as _,
                width as _,
                height as _,
                0,
                gl::RGBA,
                gl::UNSIGNED_BYTE,
                image.pixels().as_ptr() as _,
            );
            graphics.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR as _);
            graphics.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR as _);
            graphics.BindFramebufferOES(gl::FRAMEBUFFER_OES, 0);
            crate::gles::present::present_frame(graphics.as_mut(), viewport, matrix, None);
            graphics.DeleteTextures(1, &texture);
        }
        self.swap_window();
    }
    pub fn poll_for_events(&mut self, _options: &Options) {
        assert!(self.on_main_stack);
        // Bound callback work per scheduler pass, even for a broken host.
        for _ in 0..128 {
            if self.events.len() >= 256 {
                break;
            }
            let mut input = InputEvent::default();
            let result =
                unsafe { (self.surface.poll_event.unwrap())(self.surface.context, &mut input) };
            assert!(result >= 0, "Native input channel failed");
            if result == 0 {
                break;
            }
            let event = match input.kind {
                1..=3 | 9 => {
                    if !input.x.is_finite() || !input.y.is_finite() {
                        continue;
                    }
                    let (vx, vy, vw, vh) = self.viewport();
                    let x = (input.x * self.surface.width as f32 - vx as f32) / vw as f32 - 0.5;
                    let y = (input.y * self.surface.height as f32 - vy as f32) / vh as f32 - 0.5;
                    let [x, y] = self.rotation_matrix().inverse().unwrap().transform([x, y]);
                    let (w, h) = self.size_unrotated_unscaled();
                    let touches = HashMap::from([(
                        FingerId::Touch(input.finger),
                        (
                            ((x + 0.5) * w as f32).round(),
                            ((y + 0.5) * h as f32).round(),
                        ),
                    )]);
                    match input.kind {
                        1 => Event::TouchesDown(touches),
                        2 => Event::TouchesMove(touches),
                        3 => Event::TouchesUp(touches),
                        _ => Event::TouchesCancel(touches),
                    }
                }
                4 => {
                    let n = input
                        .text
                        .iter()
                        .position(|&c| c == 0)
                        .unwrap_or(input.text.len());
                    let Ok(text) = std::str::from_utf8(&input.text[..n]) else {
                        continue;
                    };
                    Event::TextInput(TextInputEvent::Text(text.to_owned()))
                }
                5 => Event::TextInput(TextInputEvent::Backspace),
                6 => Event::TextInput(TextInputEvent::Return),
                7 => Event::AppWillResignActive,
                8 => Event::Quit,
                _ => continue,
            };
            self.events.push_back(event);
        }
    }
    pub fn pop_event(&mut self) -> Option<Event> {
        self.events.pop_front()
    }
    pub fn print_accelerometer_notice(&self, _options: &Options) {}
    pub fn get_acceleration(&self, _options: &Options) -> (f32, f32, f32) {
        let mut acceleration = [0.0, 0.0, -1.0];
        if let Some(read) = self.surface.acceleration {
            unsafe {
                read(self.surface.context, acceleration.as_mut_ptr());
            }
        }
        (acceleration[0], acceleration[1], acceleration[2])
    }
    pub fn virtual_cursor_visible_at(&self) -> Option<(f32, f32, bool)> {
        None
    }
    pub fn create_gl_context(&self, version: GLVersion) -> Result<GLContext, String> {
        if !matches!(version, GLVersion::GLES11) {
            return Err("Native surface requires OpenGL ES 1.1".into());
        }
        let handle = unsafe {
            (self.surface.create_gl.unwrap())(self.surface.context, self.share_current.get())
        };
        if handle.is_null() {
            return Err("Could not allocate a native graphics context".into());
        }
        Ok(GLContext {
            surface: self.surface,
            handle,
        })
    }
    pub fn gl_get_proc_address(&self, name: &str) -> *const c_void {
        get_proc(self.surface, name)
    }
    pub fn set_share_with_current_context(&self, share: bool) {
        self.share_current.set(share);
    }
    pub unsafe fn make_gl_context_current(&self, context: &GLContext) {
        make_current(self.surface, context);
    }
    pub fn make_internal_gl_ctx_current(&mut self) -> Box<dyn GLES + '_> {
        let surface = self.surface;
        unsafe {
            self.internal
                .as_mut()
                .unwrap()
                .make_current_unchecked_for_window(
                    &mut |context| make_current(surface, context),
                    &mut |name| get_proc(surface, name),
                )
        }
    }
    pub fn swap_window(&self) {
        assert_eq!(
            unsafe { (self.surface.present.unwrap())(self.surface.context) },
            0,
            "Native frame presentation failed"
        );
    }
    pub fn rotate_device(&mut self, orientation: DeviceOrientation) {
        self.orientation = orientation;
    }
    pub fn device_family(&self) -> DeviceFamily {
        self.family
    }
    pub fn current_rotation(&self) -> DeviceOrientation {
        self.orientation
    }
    pub fn size_unrotated_unscaled(&self) -> (u32, u32) {
        self.family.portrait_size()
    }
    pub fn viewport(&self) -> (u32, u32, u32, u32) {
        let (w, h) = self.family.portrait_size();
        let (w, h) = match self.orientation {
            DeviceOrientation::Portrait | DeviceOrientation::PortraitUpsideDown => (w, h),
            _ => (h, w),
        };
        let (sw, sh) = (self.surface.width, self.surface.height);
        let scale = (sw as f32 / w as f32).min(sh as f32 / h as f32);
        let (w, h) = (
            ((w as f32 * scale).round() as u32).max(1).min(sw),
            ((h as f32 * scale).round() as u32).max(1).min(sh),
        );
        ((sw - w) / 2, (sh - h) / 2, w, h)
    }
    pub fn viewport_y_offset(&self) -> u32 {
        0
    }
    pub fn rotation_matrix(&self) -> Matrix<2> {
        match self.orientation {
            DeviceOrientation::Portrait => Matrix::identity(),
            DeviceOrientation::PortraitUpsideDown => Matrix::z_rotation(PI),
            DeviceOrientation::LandscapeLeft => Matrix::z_rotation(-FRAC_PI_2),
            DeviceOrientation::LandscapeRight => Matrix::z_rotation(FRAC_PI_2),
        }
    }
    pub fn is_screen_saver_enabled(&self) -> bool {
        self.screen_saver
    }
    pub fn set_screen_saver_enabled(&mut self, enabled: bool) {
        self.screen_saver = enabled;
        if let Some(set) = self.surface.idle_timer {
            unsafe {
                set(self.surface.context, enabled);
            }
        }
    }
    pub fn start_text_input(&self) {
        if let Some(set) = self.surface.text_input {
            unsafe {
                set(self.surface.context, true);
            }
        }
    }
    pub fn stop_text_input(&self) {
        if let Some(set) = self.surface.text_input {
            unsafe {
                set(self.surface.context, false);
            }
        }
    }
    pub fn on_main_stack(&self) -> bool {
        self.on_main_stack
    }
}
pub fn open_url(_env: &mut Environment, _url: &str) -> Result<(), String> {
    Err("External URL opening is not available in this embedded session".into())
}
pub fn show_error_messagebox(_window: Option<&Window>, message: &str) {
    log!("{message}");
}
pub fn get_battery_status() -> (i32, BatteryState) {
    (-1, BatteryState::Unknown)
}
pub fn get_preferred_language_codes(_env: &mut Environment) -> Vec<String> {
    vec!["en".into()]
}
pub fn get_preferred_country_codes(_env: &mut Environment) -> Vec<String> {
    Vec::new()
}

#[cfg(test)]
mod tests {
    use super::*;
    unsafe extern "C" fn event(context: *mut c_void, out: *mut InputEvent) -> i32 {
        let queue = unsafe { &mut *context.cast::<VecDeque<InputEvent>>() };
        if let Some(value) = queue.pop_front() {
            unsafe {
                out.write(value);
            }
            1
        } else {
            0
        }
    }
    fn window(queue: &mut VecDeque<InputEvent>) -> Window {
        Window {
            surface: Presentation {
                abi_version: 2,
                width: 320,
                height: 480,
                context: (queue as *mut VecDeque<InputEvent>).cast(),
                create_gl: None,
                destroy_gl: None,
                make_current: None,
                is_current: None,
                get_proc: None,
                present: None,
                poll_event: Some(event),
                text_input: None,
                idle_timer: None,
                acceleration: None,
                graphics_enter: None,
                graphics_leave: None,
            },
            internal: None,
            family: DeviceFamily::iPhone,
            orientation: DeviceOrientation::Portrait,
            share_current: Cell::new(false),
            screen_saver: true,
            events: VecDeque::new(),
            on_main_stack: true,
        }
    }
    #[test]
    fn raw_gles_borrow_keeps_the_host_gate_closed_until_drop() {
        use crate::gles::gles1_native::GLES1NativeContext;
        unsafe extern "C" fn enter(context: *mut c_void) {
            let depth = unsafe { &*context.cast::<Cell<usize>>() };
            depth.set(depth.get() + 1);
        }
        unsafe extern "C" fn leave(context: *mut c_void) {
            let depth = unsafe { &*context.cast::<Cell<usize>>() };
            depth.set(depth.get() - 1);
        }
        unsafe extern "C" fn create(_: *mut c_void, _: bool) -> *mut c_void {
            std::ptr::dangling_mut()
        }
        unsafe extern "C" fn destroy(_: *mut c_void, _: *mut c_void) {}
        unsafe extern "C" fn current(_: *mut c_void, _: *mut c_void) -> bool {
            true
        }
        unsafe extern "C" fn make_current(_: *mut c_void, _: *mut c_void) -> i32 {
            0
        }
        unsafe extern "C" fn proc(_: *mut c_void, _: *const std::ffi::c_char) -> *const c_void {
            std::ptr::null() // This test borrows the driver but submits no GL.
        }
        let mut depth = Cell::new(0usize);
        let mut queue = VecDeque::new();
        let mut window = window(&mut queue);
        window.surface.context = (&mut depth as *mut Cell<usize>).cast();
        window.surface.graphics_enter = Some(enter);
        window.surface.graphics_leave = Some(leave);
        window.surface.create_gl = Some(create);
        window.surface.destroy_gl = Some(destroy);
        window.surface.is_current = Some(current);
        window.surface.make_current = Some(make_current);
        window.surface.get_proc = Some(proc);
        let mut context = GLES1NativeContext::new(&mut window).unwrap();
        // Both the initial load and already-current fast path need a lease.
        for _ in 0..2 {
            let graphics = context.make_current(&mut window);
            assert_eq!(depth.get(), 1);
            drop(graphics);
            assert_eq!(depth.get(), 0);
        }
    }
    #[test]
    fn native_input_and_rotated_viewport_preserve_guest_coordinates() {
        let mut queue = VecDeque::from([InputEvent {
            kind: 1,
            finger: 9,
            x: 0.25,
            y: 0.75,
            ..Default::default()
        }]);
        let mut window = window(&mut queue);
        assert_eq!(window.viewport(), (0, 0, 320, 480));
        window.poll_for_events(&Options::default());
        match window.pop_event().unwrap() {
            Event::TouchesDown(touches) => assert_eq!(touches[&FingerId::Touch(9)], (80.0, 360.0)),
            _ => panic!("Incorrect native touch event"),
        }
        window.rotate_device(DeviceOrientation::LandscapeLeft);
        assert_eq!(window.viewport(), (0, 133, 320, 213));
        queue.push_back(InputEvent {
            kind: 3,
            finger: 9,
            x: 0.5,
            y: 0.5,
            ..Default::default()
        });
        window.poll_for_events(&Options::default());
        match window.pop_event().unwrap() {
            Event::TouchesUp(touches) => {
                let (x, y) = touches[&FingerId::Touch(9)];
                assert!((x - 160.0).abs() <= 1.0 && (y - 240.0).abs() <= 1.0);
            }
            _ => panic!("Incorrect native touch release"),
        }
    }
    #[test]
    fn cancelled_touch_is_not_a_button_release() {
        let mut queue = VecDeque::from([InputEvent {
            kind: 9,
            finger: 9,
            x: 0.25,
            y: 0.75,
            ..Default::default()
        }]);
        let mut window = window(&mut queue);
        window.poll_for_events(&Options::default());
        match window.pop_event().unwrap() {
            Event::TouchesCancel(touches) => {
                assert_eq!(touches[&FingerId::Touch(9)], (80.0, 360.0))
            }
            _ => panic!("Cancellation must not become a touch-up action"),
        }
    }
    #[test]
    fn callback_polling_is_bounded_and_malformed_text_is_ignored() {
        let mut queue = VecDeque::from([InputEvent {
            kind: 4,
            text: [255; 32],
            ..Default::default()
        }]);
        queue.extend((0..200).map(|_| InputEvent {
            kind: 5,
            ..Default::default()
        }));
        let mut window = window(&mut queue);
        window.poll_for_events(&Options::default());
        assert_eq!(window.events.len(), 127);
        assert_eq!(queue.len(), 73);
    }
}
