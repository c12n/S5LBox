# S5LBox adaptation

Upstream: https://github.com/touchHLE/touchHLE

Pinned source: `ca1de89c578e79746f33c1332e28efd106c334ba`.
This is a modified source distribution, not an upstream touchHLE release.

The Rust framework implementation is retained. CPU execution is replaced with
S5LBox's portable C ARMv6 interpreter through `integrations/touchhle-cpu`.
The original backend is retained at `src/cpu/dynarmic.rs` for reference, but
Dynarmic is not a build dependency. The library is built as a static library;
no runtime code generation or executable memory is added by the CPU adapter.

Guest `exit()` and UIKit termination return an exit status to the Rust caller
instead of terminating the host process. The environment singleton is released
after failed construction and after owned resources have been dropped. The
headless lifecycle test covers repeated launches, nonzero exits, exiting from a
worker, cleanup of a sleeping worker, and restarting after a CPU fault.
The headless C host API is declared in `integrations/touchhle-cpu/s5lbox_hle.h`.
It returns ordinary runtime errors and Rust panics to its caller, uses explicit
resource/data directories without changing the process working directory, and
supports cooperative stop/pause controls. The native probe exercises this ABI;
it is a test executable, not a user-facing app mode. The upstream fatal
coroutine-invariant abort paths and blocking native I/O still need separate
review before treating the runtime as fully isolated.

`s5lbox_hle_run_live` can read the installed app bundle and mount its real guest
container through a bounded host file-exchange callback. The guest-side file
component uses a chroot, drops root privileges, and retains real kernel-owned
descriptors and POSIX locks. The client never retries an uncertain mutation.
The file protocol, privilege drop and disconnect cleanup are covered by isolated
CI tests. App transport authentication, launch exclusion and the emulated
`fcntl` connection are still pending; this is not a user-facing shared-files mode.

Included dependency sources are pinned to OpenAL Soft
`23c8a35505fe6ab7a5c87754911a133b23ac75cf`, stb
`9f1776a36d2a3d63c52f705c3a84b372cfed4340`, and the upstream PVRTDecompress tree.
OpenAL's `alc/alu.h` and `core/uhjfilter.h` explicitly include `<cstdint>` for
newer C++ toolchains. Windows builds with CMake 4 need
`CMAKE_POLICY_VERSION_MINIMUM=3.5` for SDL, and `CFLAGS=-std=c11` keeps the pinned
SDL source from being compiled as C23 on newer GCC versions.
For iOS arm64, set `CMAKE_TOOLCHAIN_FILE_aarch64_apple_ios` to the absolute path
of `s5lbox-ios-arm64.cmake`. It selects the installed iPhoneOS SDK and avoids
the pinned CMake helper's incorrect `/` sysroot. The minimum target is iOS 13;
the library build does not require signing or JIT entitlements.

This integration is unfinished and is not included in S5LBox 1.1.0. Native iOS
presentation, lifecycle isolation, shared live guest filesystem access, and
SpringBoard launch routing are not yet connected or device-validated.

The upstream flat 4 GiB reservation failed with `ENOMEM` in the ordinary UIKit
test host on an iOS 15 device. The interpreter now backs only allocated guest
VM regions, keeping the original 32-bit guest addresses and stable native
pointers. CPU transfers and `memmove` can cross adjacent regions; borrowed
native slices must fit a contiguous backing region. Heap coalescing respects
those boundaries. Partial VM unmaps preserve surviving pointers. No extended
address-space entitlement, executable memory or host pagefile change is used.
Physical-device validation of the replacement is required before enabling it.

Preserve upstream copyright notices and component licenses. The upstream
source license is MPL-2.0; its bundled resources and dependencies have their
own licenses. The existing upstream executable license notice is retained.
The MIT license of S5LBox's interpreter does not relicense the combined work.
