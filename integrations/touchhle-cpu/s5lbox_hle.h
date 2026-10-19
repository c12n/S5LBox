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
/* One synchronous framed exchange on an authenticated guest file session.
 * Return zero and the complete response length, or nonzero on I/O failure.
 * Never reconnect/replay a request. Do not throw exceptions across this ABI.
 * The channel and context must live until run_live returns, then be closed.
 * Callback invocation is serial on the runtime worker thread. */
typedef int32_t (*s5lbox_hle_file_exchange)(void *context, const uint8_t *request,
    uint32_t request_length, uint8_t *response, uint32_t response_capacity,
    uint32_t *response_length);
typedef struct {
    uint32_t abi_version;
    void *context;
    s5lbox_hle_file_exchange exchange;
    const char *home_directory;   /* /var/mobile/Applications/<UUID> */
    const char *bundle_directory; /* One installed .app directory component */
} s5lbox_hle_live_config;
/* app_path is unused here. Resources/data still refer to the host's own
 * directories. The selected guest app must not run simultaneously in the VM;
 * this API does not itself acquire a SpringBoard launch exclusion. */
int32_t s5lbox_hle_run_live(const s5lbox_hle_config *, const s5lbox_hle_live_config *,
    const s5lbox_hle_control *, s5lbox_hle_outcome *);
/* Native callback backend. All callbacks run serially on the runtime worker;
 * UI work must be marshalled to the main thread by the host. Do not throw.
 * kind: 1 down, 2 move, 3 up/cancel, 4 UTF-8 text, 5 backspace, 6 return,
 * 7 resign-active, 8 quit. Coordinates are normalized to the complete surface.
 * poll_event returns 1 for an event, 0 for none, negative on failure.
 * The surface, callback targets and GL contexts must outlive the run. */
typedef struct {
    uint32_t kind;
    int64_t finger;
    float x,y;
    uint8_t text[32];
} s5lbox_hle_input_event;
typedef struct {
    uint32_t abi_version,width,height;
    void *context;
    void *(*create_gl)(void *,bool share_current);
    void (*destroy_gl)(void *,void *);
    int32_t (*make_current)(void *,void *);
    bool (*is_current)(void *,void *);
    const void *(*get_proc)(void *,const char *);
    int32_t (*present)(void *);
    int32_t (*poll_event)(void *,s5lbox_hle_input_event *);
    void (*text_input)(void *,bool);
    void (*idle_timer)(void *,bool);
    void (*acceleration)(void *,float *three_values);
} s5lbox_hle_presentation;
/* flags must be zero. files may be null for a host-owned test app. */
int32_t s5lbox_hle_run_presented(const s5lbox_hle_config *, const s5lbox_hle_live_config *,
    const s5lbox_hle_presentation *, const s5lbox_hle_control *, s5lbox_hle_outcome *);
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
