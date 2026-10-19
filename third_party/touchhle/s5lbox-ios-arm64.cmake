# S5LBox iOS device target. Copyright (c) 2026 j0shua-SYSON. MIT licensed.
# The pinned cmake Rust crate otherwise passes CMAKE_OSX_SYSROOT=/ for iOS.
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "iOS device architecture" FORCE)
execute_process(
    COMMAND xcrun --sdk iphoneos --show-sdk-path
    RESULT_VARIABLE s5lbox_sdk_result
    OUTPUT_VARIABLE s5lbox_ios_sdk
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT s5lbox_sdk_result EQUAL 0 OR NOT IS_DIRECTORY "${s5lbox_ios_sdk}")
    message(FATAL_ERROR "An installed iPhoneOS SDK is required")
endif()
set(CMAKE_OSX_SYSROOT "${s5lbox_ios_sdk}" CACHE PATH "iPhoneOS SDK" FORCE)
if(DEFINED ENV{IPHONEOS_DEPLOYMENT_TARGET})
    set(s5lbox_min_ios "$ENV{IPHONEOS_DEPLOYMENT_TARGET}")
else()
    set(s5lbox_min_ios "13.0")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "${s5lbox_min_ios}" CACHE STRING "Minimum iOS version" FORCE)
# Compiler probes and these dependencies are libraries, not signed apps.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
