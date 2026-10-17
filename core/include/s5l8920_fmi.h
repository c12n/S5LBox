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

/* Issue 00h, two zero column bytes, three row bytes, then 30h to chip 0..7.
 * Success means the endpoint accepted and initiated that backed page read;
 * it does not mean that the NAND array is ready or that data reached guest RAM.
 * The endpoint owns geometry, capacity checks, read state and page/OOB backing.
 * False must leave the endpoint unchanged, including for unavailable rows.
 * No endpoint/owner is retained, and callbacks must not mutate the controller
 * or board. Controller reset cancels only commands not yet delivered; it does
 * not reset a separately owned chip. Use the NAND reset command for that. */
typedef bool (*s5l8920_fmi_page_fn)(void *owner,unsigned chip,uint32_t row);
typedef struct {
    s5l8920_fmi_device_fn command;
    s5l8920_fmi_page_fn read_page;
} s5l8920_fmi_endpoint_t;

/* Additional transient NAND status capability. command performs 70h (enter
 * status mode) or 00h (return to an already issued page read). sample supplies
 * the selected chip's current status byte, including actual busy/ready state.
 * Missing/refusing callbacks leave pending work unchanged; false must not
 * mutate endpoint state. Neither callback may change the controller or board.
 * No mode switch or ready byte is supplied by the controller itself. */
typedef struct {
    bool (*command)(void *owner,unsigned chip,uint8_t command);
    bool (*sample)(void *owner,unsigned chip,uint8_t *status);
} s5l8920_fmi_status_device_t;

typedef struct {
    uint32_t timing, control, status;
    uint8_t selected, id[S5L8920_FMI_ID_SIZE], cursor, data;
    uint8_t pending;
    uint16_t programmed;
    bool initialized, enabled, reset_asserted, id_valid, data_valid, servicing;
    uint64_t commands, bytes_latched;
    uint32_t ecc_mask;
    uint32_t address, address_high, address_end;
    uint32_t command;
    uint32_t rbb_config, status_interrupt_mask, interrupt_enable, interrupt_pending;
    uint64_t status_samples;
    bool status_mode, page_issued;
} s5l8920_fmi_t;

/* First use requires a zero-initialized object. Init discards all programming.
 * Functional reset assertion cancels pending work/data and invalidates mode,
 * timing/address programming; deassertion alone does not enable the controller.
 * Timing writes at 0x408 are accepted before mode enable, as required by the
 * bootloader's initialization order. They do not enable commands or readback;
 * reset, pending work and an active byte strobe still refuse timing writes.
 * ECC mask 0x814 retains only the observed zero programming after mode enable;
 * readback requires that explicit write. Reset invalidates its programming.
 * Other mask values, ECC pending/status, mask polarity and ECC IRQ effects
 * are not modeled. Mask programming never completes or acknowledges a command.
 * Address staging retains the observed ADDR0/ADDR1/ANUM fields at 0x418/41c/420.
 * ADDR0 accepts one-byte ID addresses or zero-column page rows in bits 16..31;
 * ADDR1 accepts the high row byte, and ANUM accepts 0 or 4. Readback requires
 * explicit programming, which reset invalidates. Staging never starts work.
 * READID requires ANUM=0 and an ADDR0 that fits one byte; a staged page address
 * must not be truncated into an ID request. The paired page command 0x3000
 * queues with control 0xb only for one selected chip, explicitly programmed
 * timing, zero column, all three address fields and ANUM=4.
 * RBB_CFG 0x44c supports the observed 0x4040 status-bit6 predicate. Explicit
 * zero interrupt-mask writes at0x440/0x10 select the supported polled path;
 * nonzero interrupt enables remain unavailable. INTPND0x0c accepts the observed
 * low-four-bit W1C acknowledgement. DMA/ECC interrupt producers and INTPND
 * readback are unavailable; supported polled commands leave those sources idle.
 * Reentrant init/reset/configuration refuses without altering a transaction. */
bool s5l8920_fmi_init(s5l8920_fmi_t *f);
bool s5l8920_fmi_reset_line(s5l8920_fmi_t *f,bool asserted);
bool s5l8920_fmi_read(const s5l8920_fmi_t *f,uint32_t offset,uint32_t *value);
bool s5l8920_fmi_write(s5l8920_fmi_t *f,uint32_t offset,uint32_t value);

/* One explicit functional quantum completes one endpoint command or latches
 * one previously supplied ID byte. Reads never service work. The 0x50/0 byte
 * strobe exposes a stable byte at 0x448 until lowered; repeated reads do not
 * consume more data. Register 0x444 reports actual command completion with W1C
 * masks 1/9/0xb/0x20 (bits 0/1/3/5), plus the observed broad clear0x31ffff.
 * Page-command completion requires read_page backing
 * supplied to service_endpoint. The original service API has no such backing
 * and leaves a page command pending. service_io additionally delivers70/00
 * through explicit status backing. In status mode a0x50 strobe samples once
 * per service quantum until the configured bit6 predicate matches, then latches
 * FSTATUSbit5 and stops sampling. Reads never sample or advance NAND work.
 * The latest supplied byte is readable while the strobe is high; lowering it
 * cancels an unfinished poll, invalidates the byte, and leaves W1C flags alone.
 * Returning to page data requires a serviced page request on the selected chip.
 * Status polling requires explicit zero masks/PIO and IFCTRLbit20 clear.
 * No IRQ delivery, page/OOB data transfer, DMA, ECC processing, multiple-chip selection,
 * physical timing model or default endpoint is provided. Timing 0x408 retains
 * configuration; PIO ID strobes require its value zero and register 0x424=0.
 * Counters are diagnostic effects, not measured physical time or capacity. */
bool s5l8920_fmi_service(s5l8920_fmi_t *f,s5l8920_fmi_device_fn device,void *owner);
bool s5l8920_fmi_service_endpoint(s5l8920_fmi_t *f,
                                 const s5l8920_fmi_endpoint_t *endpoint,void *owner);
bool s5l8920_fmi_service_io(s5l8920_fmi_t *f,const s5l8920_fmi_endpoint_t *endpoint,
                           const s5l8920_fmi_status_device_t *status,void *owner);
#endif
