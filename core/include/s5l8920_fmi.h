/* Partial S5L8920 polled NAND controller.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#ifndef S5LBOX_S5L8920_FMI_H
#define S5LBOX_S5L8920_FMI_H
#include <stdbool.h>
#include <stdint.h>

#define S5L8920_FMI_BASE UINT32_C(0x81200000)
#define S5L8920_FMI_STRIDE UINT32_C(0x100000)
#define S5L8920_FMI_COUNT 2u
#define S5L8920_FMI_GATE 0x26u
#define S5L8920_FMI_ID_SIZE 8u
#define S5L8920_FMI_NO_CHIP 8u

/* A transient device capability for one selected chip (0..7), or NO_CHIP
 * when the controller clocks a command with no chip line selected. Commands are
 * 0xff reset or 0x90 READID. On READID success, supply eight bytes for the
 * explicit address. A selected reset must perform the endpoint's reset effect.
 * NO_CHIP requires explicit bus backing, including sampled data on READID;
 * it never receives an implicit zero/all-ones response or a default completion.
 * False means unavailable/unsupported with no endpoint side effects. Neither
 * the capability nor owner is retained. No default IDs or absent-chip values.
 * The callback must not change the controller or its owning board. */
typedef bool (*s5l8920_fmi_device_fn)(void *owner,unsigned chip,uint8_t command,
                                    uint8_t address,uint8_t id[S5L8920_FMI_ID_SIZE]);

typedef struct {
    uint32_t timing, control, status;
    uint8_t selected, command, address, id[S5L8920_FMI_ID_SIZE], cursor, data;
    uint8_t programmed, pending;
    bool initialized, enabled, reset_asserted, id_valid, data_valid, servicing;
    uint64_t commands, bytes_latched;
} s5l8920_fmi_t;

/* First use requires a zero-initialized object. Init discards all programming.
 * Functional reset assertion cancels pending work/data and invalidates mode,
 * timing/address programming; deassertion alone does not enable the controller.
 * Timing writes at 0x408 are accepted before mode enable, as required by the
 * bootloader's initialization order. They do not enable commands or readback;
 * reset, pending work and an active byte strobe still refuse timing writes.
 * Reentrant init/reset/configuration refuses without altering a transaction. */
bool s5l8920_fmi_init(s5l8920_fmi_t *f);
bool s5l8920_fmi_reset_line(s5l8920_fmi_t *f,bool asserted);
bool s5l8920_fmi_read(const s5l8920_fmi_t *f,uint32_t offset,uint32_t *value);
bool s5l8920_fmi_write(s5l8920_fmi_t *f,uint32_t offset,uint32_t value);

/* One explicit functional quantum completes one endpoint command or latches
 * one previously supplied ID byte. Reads never service work. The 0x50/0 byte
 * strobe exposes a stable byte at 0x448 until lowered; repeated reads do not
 * consume more data. Register 0x444 reports actual command completion with W1C
 * masks 1/9 (bits 0/3).
 * No IRQ, page/OOB access, DMA, ECC, status 0x70 command, multiple-chip selection,
 * physical timing model or default endpoint is provided. Timing 0x408 retains
 * configuration; PIO ID strobes require its value zero and register 0x424=0.
 * Counters are diagnostic effects, not measured physical time or capacity. */
bool s5l8920_fmi_service(s5l8920_fmi_t *f,s5l8920_fmi_device_fn device,void *owner);
#endif
