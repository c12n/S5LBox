/* Partial S5L8920 NAND command, status and sector controller.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#ifndef S5LBOX_S5L8920_FMI_H
#define S5LBOX_S5L8920_FMI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define S5L8920_FMI_BASE UINT32_C(0x81200000)
#define S5L8920_FMI_STRIDE UINT32_C(0x100000)
#define S5L8920_FMI_COUNT 2u
#define S5L8920_FMI_GATE 0x26u
#define S5L8920_FMI_ID_SIZE 8u
#define S5L8920_FMI_NO_CHIP 8u
/* Observed pending acknowledgement bits; no names for unproven event causes. */
#define S5L8920_FMI_ECC_PENDING_BITS UINT32_C(0x68)

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

/* Sequential bytes from the selected NAND's current page-data stream.
 * READY supplies exactly count (1..16) bytes and advances that stream; WAIT
 * and UNAVAILABLE leave it unchanged. Their output bytes are discarded.
 * The callback must not mutate the controller/board or its FIFO storage. */
typedef enum {
    S5L8920_FMI_DATA_UNAVAILABLE=0, S5L8920_FMI_DATA_WAIT=1, S5L8920_FMI_DATA_READY=2
} s5l8920_fmi_data_result_t;
typedef s5l8920_fmi_data_result_t (*s5l8920_fmi_data_fn)(void *owner,unsigned chip,
                                                      uint8_t *bytes,unsigned count);
#define S5L8920_FMI_DATA_STAGING_MAX 512u
#define S5L8920_FMI_SPARE_STAGING_MAX 32u

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
    uint32_t sector_configuration,ecc_control;
    uint16_t fifo_capacity[2],fifo_head[2],fifo_count[2],sector_remaining[2];
    uint8_t data_fifo[S5L8920_FMI_DATA_STAGING_MAX],spare_fifo[S5L8920_FMI_SPARE_STAGING_MAX];
    bool sector_pending;
    uint64_t stream_bytes,sectors_completed;
    /* Optional owner-backed functional result queue, not measured FIFO depth. */
    uint32_t *ecc_results;
    size_t ecc_capacity,ecc_head,ecc_count;
    bool ecc_clear_seen;
    uint32_t ecc_pending,ecc_pending_known;
    bool ecc_pending_configured;
} s5l8920_fmi_t;

/* First use requires a zero-initialized object. Init discards all programming.
 * Functional reset assertion cancels pending work/data and invalidates mode,
 * timing/address programming; deassertion alone does not enable the controller.
 * Timing writes at 0x408 are accepted before mode enable, as required by the
 * bootloader's initialization order. They do not enable commands or readback;
 * reset, pending work and an active byte strobe still refuse timing writes.
 * ECC mask 0x814 retains only the observed zero programming after mode enable;
 * readback requires that explicit write. Reset invalidates its programming.
 * Other mask values, mask polarity and ECC IRQ effects are not modeled.
 * Pending bits require the separate explicit policy below. Mask programming
 * never completes or acknowledges a command.
 * Address staging retains the observed ADDR0/ADDR1/ANUM fields at 0x418/41c/420.
 * ADDR0 accepts one-byte ID addresses or zero-column page rows in bits 16..31;
 * ADDR1 accepts the high row byte, and ANUM accepts 0 or 4. Readback requires
 * explicit programming, which reset invalidates. Staging never starts work.
 * READID requires ANUM=0 and an ADDR0 that fits one byte; a staged page address
 * must not be truncated into an ID request. The paired page command 0x3000
 * queues with control 0xb only for one selected chip, explicitly programmed
 * timing, zero column, all three address fields and ANUM=4.
 * RBB_CFG 0x44c supports the observed 0x4040 status-bit6 predicate. Explicit
 * event-mask bit5 at0x440 gates FSTATUSbit5 into the live INTPNDbit8 aggregate.
 * INT_EN0x10 supports bit8 and backed raw-sector bit1. Masks and low-four-bit
 * or sector-bit1 W1C at INTPND0x0c remain writable during command/data work;
 * they do not advance it. INTPND readback requires both masks programmed.
 * Its low-bit W1C does not clear the status aggregate: clear FSTATUS or mask
 * its source. Other mask/enable bits, including bit13, are unavailable.
 * Reentrant init/reset/configuration refuses without altering a transaction. */
bool s5l8920_fmi_init(s5l8920_fmi_t *f);
bool s5l8920_fmi_reset_line(s5l8920_fmi_t *f,bool asserted);
bool s5l8920_fmi_read(s5l8920_fmi_t *f,uint32_t offset,uint32_t *value);
bool s5l8920_fmi_write(s5l8920_fmi_t *f,uint32_t offset,uint32_t value);
/* Pure query of backed pending events AND explicitly programmed enables.
 * No sampling, command/data progress, default status or elapsed-time effect. */
bool s5l8920_fmi_irq(const s5l8920_fmi_t *f);

/* Optional functional ECC result-buffer policy. Select disjoint owner storage
 * before guest programming; identical repetition preserves state. Storage stays
 * valid until init, which detaches it without freeing it. Controller reset
 * preserves this host selection but discards results and the clear command.
 * This storage bound is not a physical FIFO capacity/reset-retention claim.
 * After mode enable, aligned word80C=1 clears results and opens a new result
 * sequence. Other values and clears during pending commands/data/byte strobes
 * refuse. A word read pops one supplied result; an empty/unselected/unprepared
 * queue refuses without changing output or state. No implicit zero result.
 * Supply appends one independently backed sector result: corrected count0..31
 * in bits16..20, failure flag in bit0. Full queues refuse without dropping data.
 * Calls are serialized between instructions and endpoint service. The owner
 * must derive results from actual ECC processing or explicit fixture inputs;
 * this queue neither performs ECC nor generates results from raw NAND bytes.
 * No ECC status/IRQ, chip readiness, command completion, time or DMA effect. */
bool s5l8920_fmi_ecc_configure(s5l8920_fmi_t *f,uint32_t *storage,size_t capacity);
bool s5l8920_fmi_ecc_supply(s5l8920_fmi_t *f,unsigned corrected_bits,bool failed);

/* Optional functional pending-latch policy for the observed 0x68 bit set.
 * Select before guest programming; identical repetition preserves state.
 * Selection and reset leave all supported bits UNKNOWN, not implicitly clear.
 * A word810 acknowledgement clears its written-one bits and makes those bits
 * known. Supply ORs independently backed event bits and makes those bits known.
 * Readback requires every supported bit known; reads never generate events.
 * Unsupported bits refuse atomically. A zero acknowledgement/event changes
 * nothing, including knowledge. This is a bounded software-backed policy,
 * not a silicon reset-value, complete bit-layout or event-cause assertion.
 * Controller reset retains selection but invalidates all pending knowledge;
 * init discards selection. Mode enable is required for reads, acks and events.
 * Calls are serialized between instructions and endpoint service. Pending
 * command/data/byte work is independent and is not completed or cancelled.
 * No result-queue, ECC-mask, NAND-status, INTPND, IRQ, DMA or time effects.
 * There is no automatic event producer; the owner must supply backed events. */
bool s5l8920_fmi_ecc_pending_configure(s5l8920_fmi_t *f);
bool s5l8920_fmi_ecc_pending_supply(s5l8920_fmi_t *f,uint32_t events);

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
 * Status polling requires explicit masks, zero PIO and IFCTRLbit20 clear.
 * These command services do not clock sector data or perform DMA. IRQ level
 * is queried separately by the board. No ECC processing, multiple-chip
 * selection, physical timing model
 * or default endpoint is provided. Timing 0x408 retains
 * configuration; PIO ID strobes require its value zero and register 0x424=0.
 * Counters are diagnostic effects, not measured physical time or capacity. */
bool s5l8920_fmi_service(s5l8920_fmi_t *f,s5l8920_fmi_device_fn device,void *owner);
bool s5l8920_fmi_service_endpoint(s5l8920_fmi_t *f,
                                 const s5l8920_fmi_endpoint_t *endpoint,void *owner);
bool s5l8920_fmi_service_io(s5l8920_fmi_t *f,const s5l8920_fmi_endpoint_t *endpoint,
                           const s5l8920_fmi_status_device_t *status,void *owner);

/* Explicit functional staging depths, 16..512 data and16..32 spare bytes.
 * There is no default; these bounds describe host storage, not measured silicon
 * FIFO capacity. Configure only while idle/empty; reset invalidates the depths.
 * CFG0 supports the original raw512-byte sector mode A8003 with the same spare
 * count (0..32) in bits8..13 and2..7. It also retains the original ECC page
 * constructor's 60000 base, mode16 bit14, repeated auxiliary count0..63 and
 * low page codes3/0/1/2 for software512/2048/4096/8192 bytes. Page programming
 * requires idle, empty FIFOs; it establishes no physical geometry or layout.
 * With an ECC page configuration, 808=F00 retains the observed read setup;
 * other control values and readback of CFG0/808 remain unsupported. Neither
 * write starts work or alters result/pending queues. Reset invalidates both.
 * CTRL4=3 queues one RAW sector after a backed page command in data mode.
 * Both its launch and producer service refuse ECC page execution; writes and
 * ECC decoding/correction are unimplemented. Raw reprogramming can preserve
 * buffered bytes, but switching to ECC page mode requires draining them.
 * Service clocks at most16 actual bytes into the data lane, then spare lane;
 * full FIFOs apply backpressure. INTPNDbit1 latches when all sector bytes have
 * arrived, independently of DMA stores. After W1C, another sector may queue
 * while old bytes remain buffered. No reads or old command service do this work.
 * Receive consumes only previously clocked FIFO bytes, preserving output/state
 * on WAIT/refusal. Reprogramming commands/chip/timing while pending/buffered
 * refuses. Reset discards staged bytes but does not reset the NAND endpoint.
 * No physical transfer latency, NAND geometry or ECC is inferred. */
bool s5l8920_fmi_data_configure(s5l8920_fmi_t *f,unsigned data_depth,unsigned spare_depth);
bool s5l8920_fmi_data_service(s5l8920_fmi_t *f,s5l8920_fmi_data_fn receive,void *owner);
s5l8920_fmi_data_result_t s5l8920_fmi_data_receive(s5l8920_fmi_t *f,unsigned lane,
                                                 uint8_t *bytes,unsigned count);
#endif
