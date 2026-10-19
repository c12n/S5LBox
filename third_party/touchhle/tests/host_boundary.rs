// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
use std::ffi::CString;
use std::path::Path;
use std::time::{Duration, Instant};
use touchHLE::host::*;

fn run(
    app: &Path,
    resources: &Path,
    data: &Path,
    argument: Option<&str>,
    control: *const Control,
) -> Outcome {
    let app = CString::new(app.to_str().unwrap()).unwrap();
    let resources = CString::new(resources.to_str().unwrap()).unwrap();
    let data = CString::new(data.to_str().unwrap()).unwrap();
    let arg = argument.map(|s| CString::new(s).unwrap());
    let args: Vec<_> = arg.iter().map(|s| s.as_ptr()).collect();
    let config = Config {
        abi_version: 1,
        flags: HEADLESS,
        app_path: app.as_ptr(),
        resource_directory: resources.as_ptr(),
        data_directory: data.as_ptr(),
        argument_count: args.len() as u32,
        arguments: args.as_ptr(),
    };
    let mut outcome = std::mem::MaybeUninit::uninit();
    let status = unsafe { s5lbox_hle_run(&config, control, outcome.as_mut_ptr()) };
    let outcome = unsafe { outcome.assume_init() };
    assert_eq!(status, outcome.status);
    outcome
}

#[test]
#[ignore = "requires the compiled ARMv6 fixture"]
fn paths_exit_containment_and_interruptible_execution() {
    let app = std::path::PathBuf::from(std::env::var_os("S5LBOX_HLE_SMOKE_APP").unwrap());
    let resources = Path::new(env!("CARGO_MANIFEST_DIR"))
        .canonicalize()
        .unwrap();
    let cwd = std::env::current_dir().unwrap();
    let scratch = app.parent().unwrap().join("host-boundary");
    let a = scratch.join("a");
    let b = scratch.join("b");
    std::fs::create_dir_all(&a).unwrap();
    std::fs::create_dir_all(&b).unwrap();
    for (root, arg, expected, exit_code) in [
        (&a, None, OK, 0),
        (&b, Some("--exit"), OK, 23),
        (&a, Some("--fault"), PANIC, 0),
        (&b, None, OK, 0),
    ] {
        let control = s5lbox_hle_control_create();
        let result = run(&app, &resources, root, arg, control);
        unsafe {
            s5lbox_hle_control_release(control);
        }
        assert_eq!(result.status, expected);
        assert_eq!(result.exit_code, exit_code);
        assert!(root.join("touchHLE_log.txt").is_file());
        assert_eq!(std::env::current_dir().unwrap(), cwd);
    }
    // Busy loops, long sleeps and paused sessions must all be stoppable.
    for arg in ["--spin", "--sleep", "--paused"] {
        let control = s5lbox_hle_control_create();
        if arg == "--paused" {
            unsafe {
                s5lbox_hle_control_pause(control, true);
            }
        }
        let thread_control = control as usize;
        let interrupter = std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(100));
            unsafe {
                s5lbox_hle_control_stop(thread_control as *const Control);
            }
        });
        let started = Instant::now();
        let result = run(&app, &resources, &a, Some(arg), control);
        interrupter.join().unwrap();
        unsafe {
            s5lbox_hle_control_release(control);
        }
        assert_eq!(result.status, STOPPED);
        assert!(started.elapsed() < Duration::from_secs(3), "stop stalled");
    }
    let control = s5lbox_hle_control_create();
    assert_eq!(run(&app, &resources, &b, None, control).status, OK);
    unsafe {
        s5lbox_hle_control_release(control);
    }
    println!("S5LBOX-HLE-HOST PASS");
}
