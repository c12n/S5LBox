// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
//! Uses only the locally compiled test fixture, never a user's app or guest.
use std::path::Path;

fn run(app: &Path, app_args: &[&str]) -> Result<i32, String> {
    let mut args = vec![
        "touchHLE-lifecycle-test".to_owned(),
        app.to_str().unwrap().to_owned(),
        "--headless".to_owned(),
        "--args".to_owned(),
    ];
    args.extend(app_args.iter().map(|arg| (*arg).to_owned()));
    touchHLE::run_app(args.into_iter())
}

#[test]
#[ignore = "requires the compiled ARMv6 fixture and a 4 GiB virtual address space"]
fn exits_faults_and_failed_loads_allow_another_launch() {
    let app = std::env::var_os("S5LBOX_HLE_SMOKE_APP")
        .map(std::path::PathBuf::from)
        .expect("set S5LBOX_HLE_SMOKE_APP to the compiled InterpreterSmoke.app");
    assert!(app.is_absolute());
    assert_eq!(app.file_name().unwrap(), "InterpreterSmoke.app");

    // Exercise failure after the Environment singleton has been acquired.
    let invalid = app.parent().unwrap().join("InvalidSmoke.app");
    std::fs::create_dir_all(&invalid).unwrap();
    std::fs::copy(app.join("Info.plist"), invalid.join("Info.plist")).unwrap();
    std::fs::write(invalid.join("InterpreterSmoke"), b"invalid Mach-O fixture").unwrap();
    let failed_load = std::panic::catch_unwind(|| run(&invalid, &[]));
    assert!(
        !matches!(failed_load, Ok(Ok(_))),
        "invalid executable was accepted"
    );

    // All calls run in this ONE host process. Any remaining process::exit,
    // leaked singleton, or unsafe coroutine cleanup prevents the final marker.
    for (args, status) in [
        (&[][..], 0),
        (&["--exit"][..], 23),
        (&["--thread-exit"][..], 29),
        (&["--parked-exit"][..], 31),
        (&[][..], 0),
    ] {
        assert_eq!(run(&app, args).unwrap(), status);
    }
    assert!(std::panic::catch_unwind(|| run(&app, &["--fault"])).is_err());
    assert_eq!(run(&app, &[]).unwrap(), 0);
    println!("S5LBOX-HLE-LIFECYCLE PASS");
}
