// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
//! Serial worker-side presentation callbacks. UIKit ownership stays in the host.
use std::ffi::{c_char, c_void};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct InputEvent {
    pub kind: u32,
    pub finger: i64,
    pub x: f32,
    pub y: f32,
    pub text: [u8; 32],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Presentation {
    pub abi_version: u32,
    pub width: u32,
    pub height: u32,
    pub context: *mut c_void,
    pub create_gl: Option<unsafe extern "C" fn(*mut c_void, bool) -> *mut c_void>,
    pub destroy_gl: Option<unsafe extern "C" fn(*mut c_void, *mut c_void)>,
    pub make_current: Option<unsafe extern "C" fn(*mut c_void, *mut c_void) -> i32>,
    pub is_current: Option<unsafe extern "C" fn(*mut c_void, *mut c_void) -> bool>,
    pub get_proc: Option<unsafe extern "C" fn(*mut c_void, *const c_char) -> *const c_void>,
    pub present: Option<unsafe extern "C" fn(*mut c_void) -> i32>,
    pub poll_event: Option<unsafe extern "C" fn(*mut c_void, *mut InputEvent) -> i32>,
    pub text_input: Option<unsafe extern "C" fn(*mut c_void, bool)>,
    pub idle_timer: Option<unsafe extern "C" fn(*mut c_void, bool)>,
    pub acceleration: Option<unsafe extern "C" fn(*mut c_void, *mut f32)>,
    pub graphics_enter: Option<unsafe extern "C" fn(*mut c_void)>,
    pub graphics_leave: Option<unsafe extern "C" fn(*mut c_void)>,
}
impl Presentation {
    pub fn valid(&self) -> bool {
        self.abi_version == 2
            && (1..=4096).contains(&self.width)
            && (1..=4096).contains(&self.height)
            && self.create_gl.is_some()
            && self.destroy_gl.is_some()
            && self.make_current.is_some()
            && self.is_current.is_some()
            && self.get_proc.is_some()
            && self.present.is_some()
            && self.poll_event.is_some()
            && self.graphics_enter.is_some()
            && self.graphics_leave.is_some()
    }
}

/// Holds the host's graphics gate for the entire raw-GLES borrow, not merely
/// make_current. No guest callbacks or scheduler yields occur inside a borrow.
pub struct GraphicsAccess(Presentation);
impl GraphicsAccess {
    pub fn new(surface: Presentation) -> Self {
        unsafe { (surface.graphics_enter.unwrap())(surface.context) };
        Self(surface)
    }
}
impl Drop for GraphicsAccess {
    fn drop(&mut self) {
        unsafe { (self.0.graphics_leave.unwrap())(self.0.context) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    unsafe extern "C" fn enter(context: *mut c_void) {
        let count = unsafe { &*context.cast::<Cell<i32>>() };
        count.set(count.get() + 1);
    }
    unsafe extern "C" fn leave(context: *mut c_void) {
        let count = unsafe { &*context.cast::<Cell<i32>>() };
        count.set(count.get() - 1);
    }
    #[test]
    fn graphics_borrow_releases_the_gate_on_return_and_unwind() {
        let mut depth = Cell::new(0);
        let surface = Presentation {
            abi_version: 2,
            width: 320,
            height: 480,
            context: (&mut depth as *mut Cell<i32>).cast(),
            create_gl: None,
            destroy_gl: None,
            make_current: None,
            is_current: None,
            get_proc: None,
            present: None,
            poll_event: None,
            text_input: None,
            idle_timer: None,
            acceleration: None,
            graphics_enter: Some(enter),
            graphics_leave: Some(leave),
        };
        {
            let _access = GraphicsAccess::new(surface);
            assert_eq!(depth.get(), 1);
        }
        assert_eq!(depth.get(), 0);
        let result = std::panic::catch_unwind(|| {
            let _access = GraphicsAccess::new(surface);
            panic!("simulate a rendering failure");
        });
        assert!(result.is_err());
        assert_eq!(depth.get(), 0);
    }
}
