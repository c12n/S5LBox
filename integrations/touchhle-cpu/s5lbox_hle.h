/* Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#ifndef S5LBOX_HLE_H
#define S5LBOX_HLE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct s5lbox_hle_control s5lbox_hle_control;
enum { S5LBOX_HLE_ABI_VERSION = 1, S5LBOX_HLE_HEADLESS = 1 };
enum { S5LBOX_HLE_OK = 0, S5LBOX_HLE_ERROR = 1, S5LBOX_HLE_STOPPED = 2,
       S5LBOX_HLE_BUSY = 3, S5LBOX_HLE_INVALID = 4, S5LBOX_HLE_PANIC = 5 };
typedef struct {
    uint32_t abi_version, flags;
    const char *app_path, *resource_directory, *data_directory;
    uint32_t argument_count;
    const char *const *arguments;
} s5lbox_hle_config;
typedef struct {
    int32_t status, exit_code;
    char message[512];
} s5lbox_hle_outcome;
/* Paths must be absolute, UTF-8, existing, and owned by the caller. Resource
 * and data roots must be directories. Strings/pointer arrays must remain valid
 * until run returns; output storage must be writable. Only HEADLESS is ready.
 * Run blocks its worker thread. One session per host process is supported.
 * Errors and guest exits return here; normal Rust panics do not cross the ABI.
 * This is not a security boundary for malicious native host callers. */
int32_t s5lbox_hle_run(const s5lbox_hle_config *, const s5lbox_hle_control *, s5lbox_hle_outcome *);
/* One control per run; a stopped control cannot be reset. Stop/pause may be
 * called from another thread. Release only after run and all control calls
 * have finished. Stop wakes a paused/sleeping interpreter; it is cooperative
 * and does not interrupt an in-progress native I/O call. */
const s5lbox_hle_control *s5lbox_hle_control_create(void);
void s5lbox_hle_control_release(const s5lbox_hle_control *);
void s5lbox_hle_control_stop(const s5lbox_hle_control *);
void s5lbox_hle_control_pause(const s5lbox_hle_control *, bool);
#ifdef __cplusplus
}
#endif
#endif
