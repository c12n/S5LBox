// Copyright (c) 2026 j0shua-SYSON. MIT licensed.
use std::path::Path;
fn main() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let mut build = cc::Build::new();
    build.include(root.join("core/include"));
    build.flag_if_supported("-std=c11");
    build.flag_if_supported("-ffp-contract=off");
    for name in ["arm/arm_user.c", "arm/arm_interp.c", "arm/vfp.c", "mmu.c"] {
        let path = root.join("core/src").join(name);
        println!("cargo:rerun-if-changed={}", path.display());
        build.file(path);
    }
    println!("cargo:rerun-if-changed={}", root.join("core/include").display());
    build.compile("s5lbox_user_cpu");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("linux") {
        println!("cargo:rustc-link-lib=m");
    }
}
