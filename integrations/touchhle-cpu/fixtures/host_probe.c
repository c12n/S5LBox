/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Native C caller of the Rust ABI. Only use the compiled smoke-test app. */
#include "../s5lbox_hle.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void *stop_later(void *context) {
    usleep(100000);
    s5lbox_hle_control_stop(context);
    return NULL;
}

static int check(const char *app, const char *resources, const char *data,
                 const char *argument, int expected_status, int expected_exit) {
    const s5lbox_hle_control *control = s5lbox_hle_control_create();
    s5lbox_hle_config config = {
        .abi_version = S5LBOX_HLE_ABI_VERSION, .flags = S5LBOX_HLE_HEADLESS,
        .app_path = app, .resource_directory = resources, .data_directory = data,
        .argument_count = argument ? 1 : 0, .arguments = argument ? &argument : NULL,
    };
    int interrupt = argument && (!strcmp(argument, "--spin") || !strcmp(argument, "--sleep"));
    pthread_t thread;
    if (interrupt && pthread_create(&thread, NULL, stop_later, (void *)control)) return 1;
    s5lbox_hle_outcome outcome = {0};
    int status = s5lbox_hle_run(&config, control, &outcome);
    if (interrupt) pthread_join(thread, NULL);
    s5lbox_hle_control_release(control);
    printf("HOST case=%s status=%d exit=%d message=%s\n", argument ? argument : "normal",
           status, outcome.exit_code, outcome.message);
    fflush(stdout);
    return status != expected_status || outcome.status != status || outcome.exit_code != expected_exit;
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: hle-host-probe APP RESOURCES DATA\n"); return 2; }
    int errors = 0;
    errors += check(argv[1], argv[2], argv[3], NULL, S5LBOX_HLE_OK, 0);
    errors += check(argv[1], argv[2], argv[3], "--exit", S5LBOX_HLE_OK, 23);
    errors += check(argv[1], argv[2], argv[3], "--fault", S5LBOX_HLE_PANIC, 0);
    errors += check(argv[1], argv[2], argv[3], "--spin", S5LBOX_HLE_STOPPED, 0);
    errors += check(argv[1], argv[2], argv[3], "--sleep", S5LBOX_HLE_STOPPED, 0);
    errors += check(argv[1], argv[2], argv[3], NULL, S5LBOX_HLE_OK, 0);
    printf("S5LBOX-HLE-NATIVE %s failures=%d\n", errors ? "FAIL" : "PASS", errors);
    return errors ? 1 : 0;
}
