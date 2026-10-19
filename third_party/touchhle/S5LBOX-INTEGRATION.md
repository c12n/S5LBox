# S5LBox adaptation

Upstream: https://github.com/touchHLE/touchHLE

Pinned source: `ca1de89c578e79746f33c1332e28efd106c334ba`.
This is a modified source distribution, not an upstream touchHLE release.

The Rust framework implementation is retained. CPU execution is replaced with
S5LBox's portable C ARMv6 interpreter through `integrations/touchhle-cpu`.
The original backend is retained at `src/cpu/dynarmic.rs` for reference, but
Dynarmic is not a build dependency. The library is built as a static library;
no runtime code generation or executable memory is added by the CPU adapter.

Included dependency sources are pinned to OpenAL Soft
`23c8a35505fe6ab7a5c87754911a133b23ac75cf`, stb
`9f1776a36d2a3d63c52f705c3a84b372cfed4340`, and the upstream PVRTDecompress tree.
OpenAL's `alc/alu.h` and `core/uhjfilter.h` explicitly include `<cstdint>` for
newer C++ toolchains. Windows builds with CMake 4 need
`CMAKE_POLICY_VERSION_MINIMUM=3.5` for SDL, and `CFLAGS=-std=c11` keeps the pinned
SDL source from being compiled as C23 on newer GCC versions.

This integration is unfinished and is not included in S5LBox 1.1.0. Native iOS
presentation, lifecycle isolation, shared live guest filesystem access, and
SpringBoard launch routing are not yet connected or device-validated.

The upstream Windows allocator commits a flat 4 GiB address space. A host with
insufficient commit capacity cannot run the full library's memory tests; this
does not affect the standalone C/Rust bridge tests. The full-runtime CI uses
macOS. No host pagefile settings are changed. Stock-iOS address-space and
lifecycle behavior still need separate device validation.

Preserve upstream copyright notices and component licenses. The upstream
source license is MPL-2.0; its bundled resources and dependencies have their
own licenses. The existing upstream executable license notice is retained.
The MIT license of S5LBox's interpreter does not relicense the combined work.
