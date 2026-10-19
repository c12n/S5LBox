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
}
impl Presentation {
    pub fn valid(&self) -> bool {
        self.abi_version == 1
            && (1..=4096).contains(&self.width)
            && (1..=4096).contains(&self.height)
            && self.create_gl.is_some()
            && self.destroy_gl.is_some()
            && self.make_current.is_some()
            && self.is_current.is_some()
            && self.get_proc.is_some()
            && self.present.is_some()
            && self.poll_event.is_some()
    }
}
