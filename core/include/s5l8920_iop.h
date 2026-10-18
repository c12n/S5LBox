/* Partial S5L8920 IOP version1 reset/run and firmware-window controller.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#ifndef S5LBOX_S5L8920_IOP_H
#define S5LBOX_S5L8920_IOP_H
#include "arm.h"
#include "pl192.h"
#include <stddef.h>

#define S5L8920_IOP_BASE UINT32_C(0x86300000)
#define S5L8920_IOP_RAM_BASE UINT32_C(0x40000000)
/* The target driver opens both raw gates before reset. Their individual
 * clock roles/rates are not established; only the observed pair is enabled. */
#define S5L8920_IOP_RESET_GATE 0x2eu
#define S5L8920_IOP_DEPENDENCY_GATE 0x31u
#define S5L8920_IOP_VIC_BASE UINT32_C(0xbf300000)
#define S5L8920_IOP_VIC_STRIDE UINT32_C(0x10000)
#define S5L8920_IOP_VIC_COUNT 3u
/* Shared AP/IOP gate register aperture; configuration remains board-owned. */
#define S5L8920_CLOCK_GATE_BASE UINT32_C(0xbf100078)
#define S5L8920_CLOCK_GATE_COUNT 52u

/* RAM only, no MMIO or side effects. A failed lookup returns NULL. The owner
 * keeps views valid during one controller call. Guest aliases refer to the
 * same allocation; no firmware copy, patch or mailbox implementation occurs. */
typedef uint8_t *(*s5l8920_iop_memory_fn)(void *,uint32_t,uint32_t);
/* Write-one-to-set on one of the three AP software-interrupt banks. A false
 * return must leave the receiver unchanged. This does not acknowledge a
 * message or advance the AP CPU. The owner is shared with the RAM provider. */
typedef bool (*s5l8920_iop_signal_fn)(void *,unsigned,uint32_t);
/* Pure read of the owner's currently supplied counter; never advances time. */
typedef uint64_t (*s5l8920_iop_timebase_fn)(void *);
/* Borrowed access to one existing FMI controller. Only aligned word accesses
 * in the two FMI windows reach this callback; bank/offset identify the target.
 * For writes, value is input only; successful reads supply it. False means
 * refused with no device effect. Do not mutate the IOP or its owning board,
 * service endpoints, or advance time. The owner is shared with RAM. */
typedef bool (*s5l8920_iop_fmi_fn)(void *,unsigned,uint32_t,bool,uint32_t *);
/* Borrowed SDHC access, restricted to its 4 KiB register window. Arguments
 * are owner, offset, byte width, write flag and value. As with FMI, refusal
 * must preserve device state; reads/writes never service hardware or time. */
typedef bool (*s5l8920_iop_sdhc_fn)(void *,uint32_t,unsigned,bool,uint32_t *);
/* Borrowed aligned word access to a shared gate, indexed from GATE_BASE.
 * False must preserve state. The owner checks configuration and supported
 * reset coupling; no endpoint service, time advance or CPU reset is implicit. */
typedef bool (*s5l8920_iop_gate_fn)(void *,unsigned,bool,uint32_t *);
typedef struct {
    uint32_t remaining;
    bool configured,programmed,enabled,expired,pending;
} s5l8920_iop_timer_t;
/* Resident storage for the optional functional cache policy. Sixteen-byte
 * granules follow the firmware's maintenance stride, not measured geometry. */
typedef struct {
    uint32_t address;
    uint8_t data[16];
    bool valid,dirty;
} s5l8920_iop_cache_line_t;
typedef enum {
    S5L8920_IOP_NO_FAILURE=0,
    S5L8920_IOP_UNMAPPED,
    S5L8920_IOP_MEMORY_UNAVAILABLE,
    S5L8920_IOP_REGISTER_REFUSED,
    S5L8920_IOP_CACHE_LIMIT
} s5l8920_iop_failure_reason_t;
typedef struct {
    s5l8920_iop_failure_reason_t reason;
    uint32_t address,pc,value;
    unsigned size;
    bool write;
} s5l8920_iop_failure_t;
typedef struct {
    arm_cpu_t cpu;
    arm_bus_t bus;
    s5l8920_iop_failure_t failure;
    uint32_t firmware_base,firmware_mask,firmware_size,self;
    uint64_t bytes_written;
    uint8_t programmed; /* base, mask, self */
    bool initialized,reset_seen,reset_asserted,running;
    /* Transient execution binding: never retained after service returns. */
    s5l8920_iop_memory_fn memory;
    s5l8920_iop_signal_fn signal_ap;
    s5l8920_iop_timebase_fn timebase;
    s5l8920_iop_fmi_fn fmi;
    s5l8920_iop_sdhc_fn sdhc;
    s5l8920_iop_gate_fn gate;
    void *memory_owner;
    bool servicing;
    pl192_t vic[S5L8920_IOP_VIC_COUNT];
    bool vic_configured;
    s5l8920_iop_cache_line_t *cache_lines;
    size_t cache_capacity;
    uint32_t cache_address;
    bool cache_ready,cache_enabled,cache_address_valid;
    s5l8920_iop_timer_t timer;
    uint32_t ram_size;
} s5l8920_iop_t;

/* Explicit partial ARMv4T profile, not a physical CPU/revision identification.
 * Constructor values do not establish hardware reset state: a real reset
 * pulse is required before register access or execution. No default image.
 * Calls must be serialized between AP and IOP instructions. */
void s5l8920_iop_init(s5l8920_iop_t *i);
/* Select the version1 shared RAM aperture: IOP address A maps to physical
 * 40000000+A, except the lower programmed firmware window takes precedence.
 * Size is an explicit page-aligned owner bound, at most one GiB; this does
 * not allocate RAM or infer its physical capacity. Each backing lookup still
 * requires the transient RAM-only capability. Configure before first CPU
 * reset; identical repetition preserves state. CPU reset/hold preserves this
 * selection, init discards it. No AP physical-address or MMIO aliases.
 * Transfers crossing the firmware-window boundary are refused. */
bool s5l8920_iop_ram_configure(s5l8920_iop_t *i,uint32_t size);
/* Assert resets/holds the CPU and invalidates firmware-window programming.
 * Release leaves it held until RUN. Repeated levels preserve state. RAM is
 * untouched. This is a functional reset signal, without physical latency.
 * After optional cache initialization, warm CPU reset is refused because
 * the physical reset coupling is unknown; reinitialize the component for a
 * separately requested cold reset. */
bool s5l8920_iop_reset_line(s5l8920_iop_t *i,bool asserted);
/* Version1 protocol only: +100 control/stop status, +110 physical base,
 * +114 inverse power-of-two length mask, +118 self address. Control0 stops
 * without resetting the CPU; control1 runs/resumes a fully configured RAM
 * window. The original post-reset control10 request is accepted only while
 * held at reset with no firmware programming. Bit1 reflects requested stop,
 * never an unsupported-instruction or memory failure. Other control bits,
 * live remapping and unknown initial register readbacks are refused.
 * AP access to cache registers, mailbox messages and timers is unavailable. */
bool s5l8920_iop_read(const s5l8920_iop_t *i,uint32_t offset,uint32_t *value);
bool s5l8920_iop_write(s5l8920_iop_t *i,uint32_t offset,uint32_t value,
                     s5l8920_iop_memory_fn memory,void *owner);
/* Execute at most one real instruction. Held/reset states do no work. Bus
 * failure is retained separately from the AP and does not manufacture a
 * stopped acknowledgement. No implicit CPU execution on reads/writes, and
 * no inferred ratio to AP clocks. Unsupported memory and instructions stop
 * at the original PC. Earlier successful stores in a multi-store remain.
 * This standalone form maps the low firmware window, selected shared RAM and
 * explicitly configured IOP VIC/cache/timer windows; timebase reads require the
 * separately borrowed capability below. */
arm_status_t s5l8920_iop_service(s5l8920_iop_t *i,
                               s5l8920_iop_memory_fn memory,void *owner);
/* As above, with a borrowed AP signal capability for this instruction only.
 * Only aligned word writes to BF200018/BF210018/BF220018 are routed. Reads
 * and other AP selectors remain refused; NULL keeps this mapping unavailable.
 * Neither capability nor owner survives the call, including failure. */
arm_status_t s5l8920_iop_service_with_signal(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,void *owner);
/* Also lends a read-only shared timebase for this instruction. Only a selected
 * timer exposes aligned BF100200/204 reads; NULL leaves them unavailable.
 * Each word samples the actual supplied counter independently. No callback,
 * owner or implicit clock advance survives the call, including failure. */
arm_status_t s5l8920_iop_service_with_timebase(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,void *owner);
/* Also lends FMI access for this instruction only. No arbitrary board-bus
 * forwarding or RAM fallback: only 81200000/81300000 + offsets below 1000
 * can reach the capability. Its owner enforces attachment, clocks, reset and
 * supported registers/values. NULL leaves both windows unavailable. Failure
 * belongs to the IOP, not the AP. All bindings are cleared on return. */
arm_status_t s5l8920_iop_service_with_fmi(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,s5l8920_iop_fmi_fn fmi,void *owner);
/* Also lends SDHC access to 80000000..80000FFF, for this instruction only.
 * Only aligned byte/halfword/word accesses wholly within the window reach
 * the callback. Its owner enforces presence, bus clock/reset and supported
 * register semantics. NULL keeps the window unavailable. No RAM fallback,
 * AP forwarding, interrupt wiring or reset completion is implicit. Earlier
 * standalone service APIs do not lend this capability. Board service lends
 * it only for an explicitly attached SDHC component. */
arm_status_t s5l8920_iop_service_with_sdhc(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,s5l8920_iop_fmi_fn fmi,
    s5l8920_iop_sdhc_fn sdhc,void *owner);
/* Also lends only the 52 aligned gate words at BF100078..BF100144. The
 * board shares its existing configured control values and FMI reset lines;
 * unsupported reset domains still refuse. This does not lend other power
 * manager registers or the AP bus. Self-reset during an IOP instruction is
 * refused. NULL and all earlier standalone APIs withhold this capability.
 * No binding survives the instruction, including failure. */
arm_status_t s5l8920_iop_service_with_gates(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,s5l8920_iop_fmi_fn fmi,
    s5l8920_iop_sdhc_fn sdhc,s5l8920_iop_gate_fn gate,void *owner);

/* Explicit cold functional timer1: BF10020C count, BF100224 control, VIC0
 * hardware source5. Requires configured VICs and precedes first CPU reset.
 * Repeated configuration preserves state. Count replacement rearms without
 * acknowledging pending; control bit0 enables, bit1 acknowledges. Expiry is
 * latched until acknowledgement. Zero waits for the next positive tick.
 * Count/control readbacks, other bits/widths and timer0 accesses are refused.
 * Permitted CPU reset/hold preserves this peripheral; init discards it. This
 * does not establish physical reset, gating, frequency or synchronization.
 * Supplied ticks advance only this timer, independent of CPU execution/RUN.
 * An attached board forwards its supplied timebase ticks to a selected timer.
 * Clock/configuration calls must occur between instructions. */
bool s5l8920_iop_timer_configure(s5l8920_iop_t *i);
bool s5l8920_iop_timer_clock(s5l8920_iop_t *i,uint64_t ticks);

/* Explicit generic unified write-back policy for selected shared RAM. This is
 * not a measured cache identity/geometry/replacement policy. The owner lends
 * disjoint storage, valid for the component's lifetime. Capacity bounds the
 * resident working set: a full miss stops visibly instead of guessing an
 * eviction. No default cache, background activity or timing is supplied.
 * Configure before first CPU reset; identical repetition preserves state.
 * Original IOP +20=1 initializes invalid tags once, then +00=4 enables.
 * Cached stores remain private until actual clean writes backing RAM;
 * invalidation discards even dirty data. +00=0 disables without cleaning.
 * Tags use IOP addresses: aliases share backing but have independent private
 * lines until maintenance. This is an explicit functional alias policy, not
 * a measured physical cache/coherency contract.
 * Only observed zero-valued clean/invalidate commands and aligned mapped-RAM
 * address selection are implemented. Unknown reads/values/widths, +08 wait,
 * warm init/reset and remapping after initialization remain refused. All
 * RAM writeback counts in bytes_written; private stores/invalidation do not.
 * A backing-transfer failure reports that transfer's address/size and keeps
 * earlier completed writebacks. Init detaches storage without freeing it. */
bool s5l8920_iop_cache_configure(s5l8920_iop_t *i,
    s5l8920_iop_cache_line_t *lines,size_t capacity);

/* Explicit optional cold PL192 configuration for the three observed windows.
 * Independent controller IRQ/FIQ outputs are aggregated into the IOP CPU's
 * inputs; physical daisy topology, synchronizer latency and revision are not
 * inferred. VIC configuration supplies no external peripheral levels or
 * mailbox completion; an optional timer separately drives hardware source5.
 * Configure before the first CPU reset. Repeating configuration preserves
 * state. CPU reset affects only the CPU/window; it does not reset these
 * separately configured controllers. Reinitialize the component for cold
 * reset. Call only between instructions; init discards configuration.
 * Aligned word selectors below ID registers are supported except PROTECTION,
 * because the bus has no privilege sideband. Offset is relative to VIC_BASE.
 * Reads of ADDRESS acknowledge real pending requests; writes end service.
 * Access requires a completed CPU reset and a released reset line. */
bool s5l8920_iop_vic_configure(s5l8920_iop_t *i);
bool s5l8920_iop_vic_read(s5l8920_iop_t *i,uint32_t offset,uint32_t *value);
bool s5l8920_iop_vic_write(s5l8920_iop_t *i,uint32_t offset,uint32_t value);
#endif
