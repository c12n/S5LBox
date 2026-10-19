// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
//! S5LBox host boundary. Never changes the process working directory.
use std::cell::RefCell;
use std::ffi::{c_char, CStr};
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

static RUNNING: AtomicBool = AtomicBool::new(false);

#[derive(Default)]
struct ControlState {
    paused: bool,
    stopped: bool,
}

#[derive(Default)]
pub struct Control {
    state: Mutex<ControlState>,
    wake: Condvar,
}

struct Session {
    resources: PathBuf,
    data: PathBuf,
    log: File,
    control: Arc<Control>,
}

thread_local! {
    static SESSION: RefCell<Option<Session>> = const { RefCell::new(None) };
}

struct SessionGuard;
impl Drop for SessionGuard {
    fn drop(&mut self) {
        SESSION.with(|session| {
            session.borrow_mut().take();
        });
        RUNNING.store(false, Ordering::Release);
    }
}

#[derive(Debug)]
pub(crate) struct StopRequested;

fn control() -> Option<Arc<Control>> {
    SESSION.with(|session| session.borrow().as_ref().map(|s| Arc::clone(&s.control)))
}

/// Called at interpreter/scheduler safe points, never from a C callback.
pub(crate) fn poll_control() {
    if let Some(control) = control() {
        let mut state = control.state.lock().unwrap();
        while state.paused && !state.stopped {
            state = control.wake.wait(state).unwrap();
        }
        if state.stopped {
            // Release the mutex before unwinding so the control is not poisoned.
            drop(state);
            std::panic::resume_unwind(Box::new(StopRequested));
        }
    }
}

pub(crate) fn sleep(duration: Duration) {
    if let Some(control) = control() {
        let state = control.state.lock().unwrap();
        drop(
            control
                .wake
                .wait_timeout_while(state, duration, |s| !s.stopped && !s.paused)
                .unwrap(),
        );
        poll_control();
    } else {
        std::thread::sleep(duration);
    }
}

pub(crate) fn resource_path(path: &str) -> Option<PathBuf> {
    SESSION.with(|session| session.borrow().as_ref().map(|s| s.resources.join(path)))
}

pub(crate) fn data_path() -> Option<PathBuf> {
    SESSION.with(|session| session.borrow().as_ref().map(|s| s.data.clone()))
}

pub(crate) fn write_log(bytes: &[u8]) -> bool {
    SESSION.with(|session| {
        if let Some(session) = session.borrow_mut().as_mut() {
            let _ = session.log.write_all(bytes);
            true
        } else {
            false
        }
    })
}

pub const OK: i32 = 0;
pub const ERROR: i32 = 1;
pub const STOPPED: i32 = 2;
pub const BUSY: i32 = 3;
pub const INVALID: i32 = 4;
pub const PANIC: i32 = 5;
pub const HEADLESS: u32 = 1;

#[repr(C)]
pub struct Config {
    pub abi_version: u32,
    pub flags: u32,
    pub app_path: *const c_char,
    pub resource_directory: *const c_char,
    pub data_directory: *const c_char,
    pub argument_count: u32,
    pub arguments: *const *const c_char,
}

#[repr(C)]
pub struct Outcome {
    pub status: i32,
    pub exit_code: i32,
    pub message: [c_char; 512],
}

impl Outcome {
    fn new(status: i32, exit_code: i32, message: &str) -> Self {
        let mut result = Self {
            status,
            exit_code,
            message: [0; 512],
        };
        let mut count = message.len().min(result.message.len() - 1);
        while !message.is_char_boundary(count) {
            count -= 1;
        }
        for (dst, src) in result.message.iter_mut().zip(&message.as_bytes()[..count]) {
            *dst = *src as c_char;
        }
        result
    }
}

unsafe fn string(pointer: *const c_char) -> Result<String, String> {
    if pointer.is_null() {
        return Err("Missing UTF-8 string".into());
    }
    let text = unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .map_err(|_| "Invalid UTF-8 string")?;
    if text.len() > 32768 {
        return Err("String exceeds the host API limit".into());
    }
    Ok(text.to_owned())
}

fn absolute_existing(path: &str, directory: bool) -> Result<PathBuf, String> {
    let path = Path::new(path);
    if !path.is_absolute() {
        return Err("Host paths must be absolute".into());
    }
    let path = path.canonicalize().map_err(|e| e.to_string())?;
    if directory && !path.is_dir() {
        return Err("Expected a directory".into());
    }
    Ok(path)
}

unsafe fn run(config: &Config, control: *const Control) -> Outcome {
    // GUI mode is added only when the native presentation adapter is ready.
    if config.abi_version != 1
        || config.flags != HEADLESS
        || control.is_null()
        || config.argument_count > 128
        || (config.argument_count != 0 && config.arguments.is_null())
    {
        return Outcome::new(INVALID, 0, "Invalid host configuration");
    }
    let prepared = (|| -> Result<_, String> {
        let app = absolute_existing(&unsafe { string(config.app_path)? }, false)?;
        let resources = absolute_existing(&unsafe { string(config.resource_directory)? }, true)?;
        let data = absolute_existing(&unsafe { string(config.data_directory)? }, true)?;
        let mut args = vec![
            "S5LBox".into(),
            app.to_str().ok_or("Invalid app path")?.into(),
            "--headless".into(),
            "--args".into(),
        ];
        for i in 0..config.argument_count as usize {
            args.push(unsafe { string(*config.arguments.add(i))? });
        }
        Ok((resources, data, args))
    })();
    let (resources, data, args) = match prepared {
        Ok(value) => value,
        Err(error) => return Outcome::new(INVALID, 0, &error),
    };
    if RUNNING
        .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
        .is_err()
    {
        return Outcome::new(BUSY, 0, "Another HLE session is running");
    }
    let _session_guard = SessionGuard;
    let log = match File::create(data.join("touchHLE_log.txt")) {
        Ok(file) => file,
        Err(error) => return Outcome::new(ERROR, 0, &error.to_string()),
    };
    unsafe {
        Arc::increment_strong_count(control);
    }
    let control = unsafe { Arc::from_raw(control) };
    SESSION.with(|session| {
        *session.borrow_mut() = Some(Session {
            resources,
            data,
            log,
            control,
        })
    });
    poll_control();
    match crate::run_app(args.into_iter()) {
        Ok(status) => Outcome::new(OK, status, ""),
        Err(error) => Outcome::new(ERROR, 0, &error),
    }
}

/// All pointer lifetimes and ownership rules are specified in the C header.
#[no_mangle]
pub unsafe extern "C" fn s5lbox_hle_run(
    config: *const Config,
    control: *const Control,
    result: *mut Outcome,
) -> i32 {
    if result.is_null() {
        return INVALID;
    }
    if config.is_null() {
        unsafe {
            result.write(Outcome::new(INVALID, 0, "Missing host configuration"));
        }
        return INVALID;
    }
    let outcome = match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| unsafe {
        run(&*config, control)
    })) {
        Ok(result) => result,
        Err(payload) if payload.is::<StopRequested>() => Outcome::new(STOPPED, 0, "Stopped"),
        Err(payload) => {
            let message = payload
                .downcast_ref::<String>()
                .map(String::as_str)
                .or_else(|| payload.downcast_ref::<&str>().copied())
                .unwrap_or("HLE runtime failed");
            Outcome::new(PANIC, 0, message)
        }
    };
    let status = outcome.status;
    unsafe {
        result.write(outcome);
    }
    status
}

#[no_mangle]
pub extern "C" fn s5lbox_hle_control_create() -> *const Control {
    Arc::into_raw(Arc::new(Control::default()))
}

#[no_mangle]
pub unsafe extern "C" fn s5lbox_hle_control_release(control: *const Control) {
    if !control.is_null() {
        drop(unsafe { Arc::from_raw(control) });
    }
}

#[no_mangle]
pub unsafe extern "C" fn s5lbox_hle_control_stop(control: *const Control) {
    if let Some(control) = unsafe { control.as_ref() } {
        let mut state = control.state.lock().unwrap_or_else(|e| e.into_inner());
        state.stopped = true;
        control.wake.notify_all();
    }
}

#[no_mangle]
pub unsafe extern "C" fn s5lbox_hle_control_pause(control: *const Control, paused: bool) {
    if let Some(control) = unsafe { control.as_ref() } {
        let mut state = control.state.lock().unwrap_or_else(|e| e.into_inner());
        state.paused = paused;
        control.wake.notify_all();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn null_configuration_has_a_defined_error_result() {
        let mut output = std::mem::MaybeUninit::uninit();
        assert_eq!(
            unsafe { s5lbox_hle_run(std::ptr::null(), std::ptr::null(), output.as_mut_ptr()) },
            INVALID
        );
        let output = unsafe { output.assume_init() };
        assert_eq!(output.status, INVALID);
        assert_eq!(output.message[511], 0);
    }

    #[test]
    fn outcome_truncation_preserves_utf8() {
        let message = "\u{1f34e}".repeat(200);
        let outcome = Outcome::new(ERROR, 0, &message);
        let bytes: Vec<u8> = outcome
            .message
            .iter()
            .take_while(|&&c| c != 0)
            .map(|&c| c as u8)
            .collect();
        assert!(std::str::from_utf8(&bytes).is_ok());
        assert!(bytes.len() < 512);
    }
}
