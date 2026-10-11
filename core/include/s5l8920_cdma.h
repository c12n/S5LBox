/* Partial S5L8920 CDMA memory-to-memory AES path.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#ifndef S5LBOX_S5L8920_CDMA_H
#define S5LBOX_S5L8920_CDMA_H
#include "aes.h"

#define S5L8920_CDMA_BASE UINT32_C(0x87000000)
#define S5L8920_AES_CONTEXT1 UINT32_C(0x87801000)

/* Return a contiguous RAM view, or NULL without side effects. Never MMIO.
 * Views returned by one owner must refer to the same backing allocation. */
typedef uint8_t *(*s5l8920_cdma_memory_fn)(void *,uint32_t,uint32_t);
typedef struct {
    uint32_t control, remaining, descriptor, address, terminal;
    bool reset, size_programmed, descriptor_programmed, started;
} s5l8920_cdma_channel_t;
typedef struct {
    s5l8920_cdma_channel_t channel[2]; /* Channels 1 (feed) and 2 (drain). */
    uint32_t setup;
    uint8_t key[32], iv[16], chain[16], key_programmed, iv_programmed;
    aes_ctx_t cipher;
    bool initialized, setup_programmed;
} s5l8920_cdma_t;

/* No default key, IV, context or channel register values. Functional reset
 * discards an in-flight operation and invalidates all programming. */
void s5l8920_cdma_init(s5l8920_cdma_t *c);
bool s5l8920_cdma_read(const s5l8920_cdma_t *c,bool aes,uint32_t offset,uint32_t *value);
bool s5l8920_cdma_write(s5l8920_cdma_t *c,bool aes,uint32_t offset,uint32_t value,
                      s5l8920_cdma_memory_fn memory,void *owner);
/* One explicit work quantum processes at most one real AES block. This is
 * functional scheduling, not a measured hardware clock or transfer latency.
 * Reads and CPU execution never service DMA. Waiting/idle is successful;
 * unavailable RAM refuses without consuming a block or declaring completion.
 * Only context1, custom-key CBC, channels1/2, one whole-block data descriptor
 * per channel followed by a terminal descriptor are supported. In-place is
 * allowed; partial overlap, hardware keys, peripherals, scatter/gather,
 * live reprogramming, hold/abort, interrupt mode and other channel/context
 * modes refuse. This is the bootloader's polled memory transfer path only. */
bool s5l8920_cdma_service(s5l8920_cdma_t *c,s5l8920_cdma_memory_fn memory,void *owner);
#endif
