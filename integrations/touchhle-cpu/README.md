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

This is an integration component, not a usable touchHLE app mode. Connecting
the Rust frameworks, native iOS lifecycle and rendering, and live guest
filesystem is separate work. It is not part of the 1.1.0 release.
