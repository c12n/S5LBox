// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
//! S5LBox host boundary. Never changes the process working directory.
use std::cell::RefCell;
use std::collections::HashSet;
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
    // Guarded by the same mutex as pause/stop. Removal precedes destruction,
    // so a concurrent host pause can never use a freed OpenAL device.
    audio_devices: HashSet<usize>,
}
impl ControlState {
    fn update_audio(&self) {
        for &device in &self.audio_devices {
            crate::audio::openal::set_device_paused(device as *mut _, self.paused || self.stopped);
        }
    }
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
    live: Option<(crate::fs::live::LiveFs, String, String)>,
    presentation: Option<crate::presentation::Presentation>,
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

pub(crate) fn register_audio_device(device: *mut std::ffi::c_void) {
    if let Some(control) = control() {
        let mut state = control.state.lock().unwrap_or_else(|e| e.into_inner());
        state.audio_devices.insert(device as usize);
        // A device opened while paused must not start when its first context
        // or source is created later.
        if state.paused || state.stopped {
            crate::audio::openal::set_device_paused(device, true);
        }
    }
}
pub(crate) fn unregister_audio_device(device: *mut std::ffi::c_void) -> bool {
    control().is_some_and(|control| {
        let mut state = control.state.lock().unwrap_or_else(|e| e.into_inner());
        state.audio_devices.remove(&(device as usize))
    })
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

pub(crate) fn live_bundle() -> Option<(crate::fs::live::LiveFs, String, String)> {
    SESSION.with(|session| session.borrow().as_ref().and_then(|s| s.live.clone()))
}

pub(crate) fn presentation() -> Option<crate::presentation::Presentation> {
    SESSION.with(|session| session.borrow().as_ref().and_then(|s| s.presentation))
}

pub type FileExchange =
    unsafe extern "C" fn(*mut std::ffi::c_void, *const u8, u32, *mut u8, u32, *mut u32) -> i32;

#[repr(C)]
pub struct LiveConfig {
    pub abi_version: u32,
    pub context: *mut std::ffi::c_void,
    pub exchange: Option<FileExchange>,
    pub home_directory: *const c_char,
    pub bundle_directory: *const c_char,
}

struct HostFileExchange {
    context: *mut std::ffi::c_void,
    exchange: FileExchange,
}
impl crate::fs::live::Exchange for HostFileExchange {
    fn exchange(&mut self, request: &[u8], response: &mut [u8]) -> std::io::Result<usize> {
        let mut count = 0u32;
        let status = unsafe {
            (self.exchange)(
                self.context,
                request.as_ptr(),
                request.len() as u32,
                response.as_mut_ptr(),
                response.len() as u32,
                &mut count,
            )
        };
        if status != 0 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::ConnectionAborted,
                format!("Guest file transport failed ({status})"),
            ));
        }
        Ok(count as usize)
    }
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

unsafe fn run(
    config: &Config,
    control: *const Control,
    live: Option<&LiveConfig>,
    presentation: Option<&crate::presentation::Presentation>,
) -> Outcome {
    if config.abi_version != 1
        || config.flags != if presentation.is_some() { 0 } else { HEADLESS }
        || presentation.is_some_and(|p| !p.valid())
        || (presentation.is_some() && !cfg!(any(target_os = "ios", feature = "native-window")))
        || control.is_null()
        || config.argument_count > 128
        || (config.argument_count != 0 && config.arguments.is_null())
    {
        return Outcome::new(INVALID, 0, "Invalid host configuration");
    }
    let prepared = (|| -> Result<_, String> {
        let live = if let Some(live) = live {
            if live.abi_version != 1 {
                return Err("Invalid live-files ABI version".into());
            }
            let exchange = live
                .exchange
                .ok_or("Missing guest file exchange callback")?;
            let home = unsafe { string(live.home_directory)? };
            let directory = unsafe { string(live.bundle_directory)? };
            Some((
                crate::fs::live::LiveFs::new(Box::new(HostFileExchange {
                    context: live.context,
                    exchange,
                })),
                home,
                directory,
            ))
        } else {
            None
        };
        let app = if let Some((_, home, directory)) = &live {
            format!("{home}/{directory}")
        } else {
            absolute_existing(&unsafe { string(config.app_path)? }, false)?
                .to_str()
                .ok_or("Invalid app path")?
                .to_owned()
        };
        let resources = absolute_existing(&unsafe { string(config.resource_directory)? }, true)?;
        let data = absolute_existing(&unsafe { string(config.data_directory)? }, true)?;
        let mut args = vec!["S5LBox".into(), app];
        if presentation.is_none() {
            args.push("--headless".into());
        }
        args.push("--args".into());
        for i in 0..config.argument_count as usize {
            args.push(unsafe { string(*config.arguments.add(i))? });
        }
        Ok((resources, data, args, live))
    })();
    let (resources, data, args, live) = match prepared {
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
            live,
            presentation: presentation.copied(),
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
    unsafe { run_boundary(config, control, result, None, None) }
}

/// The host owns the authenticated channel and closes it after this returns.
#[no_mangle]
pub unsafe extern "C" fn s5lbox_hle_run_live(
    config: *const Config,
    files: *const LiveConfig,
    control: *const Control,
    result: *mut Outcome,
) -> i32 {
    if files.is_null() {
        if !result.is_null() {
            unsafe {
                result.write(Outcome::new(INVALID, 0, "Missing live-files configuration"));
            }
        }
        return INVALID;
    }
    unsafe { run_boundary(config, control, result, Some(&*files), None) }
}

/// Native surface; files may be null to run an ordinary host test bundle.
#[no_mangle]
pub unsafe extern "C" fn s5lbox_hle_run_presented(
    config: *const Config,
    files: *const LiveConfig,
    presentation: *const crate::presentation::Presentation,
    control: *const Control,
    result: *mut Outcome,
) -> i32 {
    if presentation.is_null() {
        if !result.is_null() {
            unsafe {
                result.write(Outcome::new(INVALID, 0, "Missing native surface"));
            }
        }
        return INVALID;
    }
    unsafe {
        run_boundary(
            config,
            control,
            result,
            files.as_ref(),
            presentation.as_ref(),
        )
    }
}

unsafe fn run_boundary(
    config: *const Config,
    control: *const Control,
    result: *mut Outcome,
    live: Option<&LiveConfig>,
    presentation: Option<&crate::presentation::Presentation>,
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
        run(&*config, control, live, presentation)
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
        state.update_audio();
        control.wake.notify_all();
    }
}

#[no_mangle]
pub unsafe extern "C" fn s5lbox_hle_control_pause(control: *const Control, paused: bool) {
    if let Some(control) = unsafe { control.as_ref() } {
        let mut state = control.state.lock().unwrap_or_else(|e| e.into_inner());
        if state.paused != paused {
            state.paused = paused;
            state.update_audio();
        }
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

#[cfg(test)]
mod resource_tests {
    use super::*;
    use crate::audio::openal::{OpenALContext, OpenALManager};
    use touchHLE_openal_soft_wrapper as al;

    struct TestSession(PathBuf);
    impl Drop for TestSession {
        fn drop(&mut self) {
            SESSION.with(|session| {
                session.borrow_mut().take();
            });
            let _ = std::fs::remove_file(&self.0);
        }
    }
    fn offset(source: u32) -> i32 {
        let mut position = -1;
        unsafe {
            al::alGetSourcei(source, 0x1025 /* AL_SAMPLE_OFFSET */, &mut position);
        }
        assert_eq!(unsafe { al::alGetError() }, 0);
        position
    }
    fn wait_for_audio(source: u32, previous: i32) {
        let deadline = std::time::Instant::now() + Duration::from_secs(2);
        while offset(source) == previous {
            assert!(
                std::time::Instant::now() < deadline,
                "Audio mixer did not resume"
            );
            std::thread::sleep(Duration::from_millis(10));
        }
    }

    #[test]
    #[ignore = "requires ALSOFT_DRIVERS=null to avoid host audio output"]
    fn embedded_pause_freezes_audio_and_new_devices_inherit_it() {
        assert_eq!(std::env::var("ALSOFT_DRIVERS").as_deref(), Ok("null"));
        let control = Arc::new(Control::default());
        let unique = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path =
            std::env::temp_dir().join(format!("s5lbox-audio-{}-{unique}.log", std::process::id()));
        let log = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&path)
            .unwrap();
        SESSION.with(|session| {
            assert!(session.borrow().is_none());
            *session.borrow_mut() = Some(Session {
                resources: PathBuf::new(),
                data: PathBuf::new(),
                log,
                control: Arc::clone(&control),
                live: None,
                presentation: None,
            });
        });
        let _session = TestSession(path);
        let mut manager = OpenALManager::new().unwrap();
        for iteration in 0..2 {
            let mut context = OpenALContext::new(&mut manager).unwrap();
            assert_eq!(control.state.lock().unwrap().audio_devices.len(), 1);
            let _ = context.make_current(&mut manager);
            let (mut buffer, mut source) = (0, 0);
            let silence = vec![0i16; 96000];
            unsafe {
                al::alGenBuffers(1, &mut buffer);
                al::alBufferData(
                    buffer,
                    al::al_defines::AL_FORMAT_MONO16,
                    silence.as_ptr().cast(),
                    (silence.len() * 2) as i32,
                    48000,
                );
                al::alGenSources(1, &mut source);
                al::alSourcei(source, 0x1009 /* AL_BUFFER */, buffer as i32);
                al::alSourcei(source, 0x1007 /* AL_LOOPING */, 1);
                al::alSourcePlay(source);
            }
            if iteration == 0 {
                wait_for_audio(source, 0);
                // Host lifecycle calls arrive from another thread.
                let host_control = Arc::clone(&control);
                std::thread::spawn(move || unsafe {
                    s5lbox_hle_control_pause(Arc::as_ptr(&host_control), true);
                })
                .join()
                .unwrap();
            }
            let paused = offset(source);
            std::thread::sleep(Duration::from_millis(60));
            assert_eq!(offset(source), paused, "Paused playback advanced");
            if iteration == 1 {
                assert_eq!(paused, 0, "A new device bypassed pause");
            }
            unsafe {
                s5lbox_hle_control_pause(Arc::as_ptr(&control), false);
            }
            wait_for_audio(source, paused);
            if iteration == 0 {
                unsafe {
                    s5lbox_hle_control_pause(Arc::as_ptr(&control), true);
                }
            } else {
                unsafe {
                    s5lbox_hle_control_stop(Arc::as_ptr(&control));
                    s5lbox_hle_control_pause(Arc::as_ptr(&control), false);
                }
                let stopped = offset(source);
                std::thread::sleep(Duration::from_millis(60));
                assert_eq!(offset(source), stopped, "Stop must not resume playback");
            }
            unsafe {
                al::alDeleteSources(1, &source);
                al::alDeleteBuffers(1, &buffer);
            }
            drop(context);
            assert!(control.state.lock().unwrap().audio_devices.is_empty());
        }
    }
}
