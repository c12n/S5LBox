# touchHLE interpreter bridge

This crate exposes S5LBox's ARMv6 user-mode interpreter to Rust. It executes
ARM and Thumb instructions without generating host code or requesting
executable memory. The existing full-system emulator remains unchanged.

`Cpu` owns one interpreter on one worker thread. A `Memory` provider supplies
bounded reads and writes during execution; its callbacks cannot unwind into C.
Execution returns on budget exhaustion, SVC, breakpoint, undefined instruction,
or memory failure. Thread contexts include general registers, VFP state, and
thread-local registers. Faults remain latched until a context is restored.

Run `cargo test --manifest-path integrations/touchhle-cpu/Cargo.toml --release`
from the repository root. The CMake `arm_user` test exercises the C boundary.

## Integration status

The feature branch includes a separate-container iPhone test app, built with
`app/project-touchhle.yml`. Machine Settings → touchHLE discovers installed
guest apps through `installation_proxy`. Its guest support package installs a
SpringBoard launch guard and resprings the guest. App binaries, resources,
documents and preferences then use the running guest's file service, not a
host-side sandbox or a disk-image editor.

On 2026-10-20, the integrated build passed CI and the following path passed on
an A9 iPhone running iOS 15.8.5 with a freshly jailbroken 3.1.3 guest:

- Install the ARMv6 `HLEVisual` fixture through virtual USB and discover it.
- Install guest support through the package helper and launch with native
  graphics and touch input.
- Save a document in touchHLE, read and change it from the native guest app,
  then read the updated value in touchHLE.
- Reject a touchHLE launch while the native app is running, then allow it
  after the native app exits.
- Return with Home and preserve preference writes made by termination
  callbacks across both runtimes.
- Save/close the guest, update the host test app, resume the guest, and reopen
  the same app and saved data through a newly connected file session.

The host tests additionally cover file isolation and locks, launch races,
installer validation/removal, runtime exits/faults, and audio resource cleanup.
The audio cleanup test uses a null output device; it does not verify playback.

This is **not release-ready** and is not part of 1.1.0. The device evidence
above uses a small test app, not a representative compatibility suite.
Background GPU quiescence, interruption/disconnection recovery, integrated
audio behavior and real-app compatibility still need work. Launching from
SpringBoard into touchHLE is not implemented; the test mode is opened from
Machine Settings. Do not enable it in the shipping app yet.
