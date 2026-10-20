/* Copyright (c) 2026 j0shua-SYSON. MIT licensed. Guest-only launch exclusion. */
#ifndef S5LBOX_GUEST_HLE_LAUNCH_GUARD_H
#define S5LBOX_GUEST_HLE_LAUNCH_GUARD_H
#ifndef S5LBOX_HLE_GUARD_PATH
#define S5LBOX_HLE_GUARD_PATH "/var/run/s5lbox-hle-launch-v1.lock"
#endif
#ifndef S5LBOX_HLE_GUARD_PORT
#define S5LBOX_HLE_GUARD_PORT "com.j0shua.s5lbox.hle-launch-v1"
#endif
#ifndef S5LBOX_HLE_ROUTE_PATH
#define S5LBOX_HLE_ROUTE_PATH "/var/run/s5lbox-hle-route-v1.request"
#endif
#include "guest_hle_routes.h"
/* Root file broker: acquire before entering the app sandbox. A live
 * SpringBoard hook must confirm quiescence while this descriptor excludes
 * new native user-app launches. Caller retains it until all app files close.
 * Returns an owned descriptor, or -1 with errno; never kills a guest app. */
int s5lbox_hle_launch_acquire(const char uuid[37]);
/* Authenticated package-helper operation. Returns response length or -1. */
int s5lbox_hle_route_exchange(const unsigned char *,size_t,unsigned char [HLE_ROUTE_MAX_REPLY]);
#endif
