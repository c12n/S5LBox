/* Optional read-only raw-page backing for the S5L8920 NAND bus.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#ifndef S5LBOX_S5L8920_NAND_H
#define S5LBOX_S5L8920_NAND_H
#include "s5l8920_fmi.h"

typedef struct {
    uint32_t row;
    const uint8_t *bytes;
} s5l8920_nand_page_t;
typedef struct {
    const s5l8920_nand_page_t *pages;
    size_t page_count;
    uint32_t row_count, row_bytes;
    uint8_t id[S5L8920_FMI_ID_SIZE];
    uint8_t busy_status, ready_status;
    uint8_t *cache;
    size_t cache_capacity;
} s5l8920_nand_config_t;
typedef struct {
    s5l8920_nand_config_t config;
    const uint8_t *source;
    uint32_t copied, cursor;
    uint8_t pending;
    bool configured, reset_seen, cache_valid, status_mode;
} s5l8920_nand_chip_t;
typedef struct {
    s5l8920_nand_chip_t chip[S5L8920_FMI_NO_CHIP];
    bool initialized;
} s5l8920_nand_t;

/* No default device, geometry, IDs, erased rows or status pattern. Init detaches
 * all owner storage without touching it. Configure explicitly before use;
 * identical repetition preserves state, changing a configured chip refuses.
 * Rows must be unique, sorted, below row_count (1..2^24), and each provide
 * exactly row_bytes raw bytes. Unlisted rows are unavailable, never all-ones.
 * Busy status must have bit6 clear, ready status bit6 set. Other bits are
 * explicit owner knowledge, not inferred reset values or generated ECC.
 * Configuration alone does not make status known: issue and service FF first.
 *
 * The configuration is copied, but its descriptors and bytes must remain
 * immutable and alive until init. Cache storage must remain writable and alive;
 * it must not overlap the bus, any descriptor/page bytes or another cache.
 * Only row_bytes of cache_capacity are used. Calls and external access must be
 * serialized. Output buffers must be disjoint from these borrowed objects.
 * There is no allocation, filesystem I/O or snapshot serialization here. */
bool s5l8920_nand_init(s5l8920_nand_t *n);
bool s5l8920_nand_configure(s5l8920_nand_t *n,unsigned chip,
                           const s5l8920_nand_config_t *config);

/* One explicit functional quantum completes a queued reset or copies at most
 * 16 real bytes into the page cache. A page becomes ready only on the final
 * copy. No physical latency is implied; controller clocks are independent.
 * Reads, commands and status samples never service array work. FF cancels a
 * partial read, invalidates its cache/stream, and queues a reset; it does not
 * change backing bytes. Controller reset alone does not reset this endpoint. */
bool s5l8920_nand_service(s5l8920_nand_t *n,unsigned chip);

/* Directly compatible with the transient FMI command/status/data interfaces.
 * owner is the configured bus. Only selected chips0..7 are supported. READID
 * accepts address0 after reset completion; the eight bytes are explicit input.
 * Page reads require an idle, reset chip and a listed row.70 selects status;
 * 00 returns to a completed page, preserving the sequential byte cursor.
 * A pending read returns WAIT in data mode; status mode and absent/invalid/
 * exhausted streams return UNAVAILABLE. Failures leave output/state unchanged.
 * This endpoint supplies raw bytes only: no ECC, FTL, program/erase or wiring
 * to a default board profile. Callers explicitly pass these capabilities. */
extern const s5l8920_fmi_endpoint_t s5l8920_nand_endpoint;
extern const s5l8920_fmi_status_device_t s5l8920_nand_status;
s5l8920_fmi_data_result_t s5l8920_nand_receive(void *owner,unsigned chip,
                                             uint8_t *bytes,unsigned count);
#endif
