/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Bounded, ephemeral SpringBoard routing. No disk paths or shell commands. */
#ifndef S5LBOX_GUEST_HLE_ROUTES_H
#define S5LBOX_GUEST_HLE_ROUTES_H
#include <stddef.h>
#include <stdint.h>
#define HLE_ROUTE_MAX_APPS 128
#define HLE_ROUTE_MAX_REQUEST (27+HLE_ROUTE_MAX_APPS*257)
#define HLE_ROUTE_MAX_REPLY 273
typedef struct {
    unsigned char session[16];
    char selected[HLE_ROUTE_MAX_APPS][256], pending[256];
    unsigned count;
    uint64_t serial,ticket,last_ack;
    double lease_until,ticket_until;
} s5lbox_hle_routes;
/* Request: operation:u8, session:16, ticket:BE64, count:BE16,
 * then count (length:BE16, ASCII bundle identifier). Operations:
 * 1 replace selections/renew/poll, 2 acknowledge, 3 native launch, 4 disarm.
 * Reply: HLR1, errno:BE32, ticket:BE64, identifier length:BE16, identifier.
 * Every request must first be authorized by the privileged broker. */
size_t s5lbox_hle_route_request(s5lbox_hle_routes *,const unsigned char *,size_t,
    double,unsigned char [HLE_ROUTE_MAX_REPLY],int (*)(void *,const char *),void *);
/* Main-thread icon hook: 1 means queued/duplicate; 0 preserves native launch. */
int s5lbox_hle_route_icon(s5lbox_hle_routes *,const char *,double);
#endif
