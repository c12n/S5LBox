/* Partial S5L8920 IOP version1 reset/run and firmware-window controller.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#ifndef S5LBOX_S5L8920_IOP_H
#define S5LBOX_S5L8920_IOP_H
#include "arm.h"

#define S5L8920_IOP_BASE UINT32_C(0x86300000)
/* The target driver opens both raw gates before reset. Their individual
 * clock roles/rates are not established; only the observed pair is enabled. */
#define S5L8920_IOP_RESET_GATE 0x2eu
#define S5L8920_IOP_DEPENDENCY_GATE 0x31u

/* RAM only, no MMIO or side effects. A failed lookup returns NULL. The owner
 * keeps views valid during one controller call. Guest aliases refer to the
 * same allocation; no firmware copy, patch or mailbox implementation occurs. */
typedef uint8_t *(*s5l8920_iop_memory_fn)(void *,uint32_t,uint32_t);
typedef enum {
    S5L8920_IOP_NO_FAILURE=0,
    S5L8920_IOP_UNMAPPED,
    S5L8920_IOP_MEMORY_UNAVAILABLE
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
    void *memory_owner;
    bool servicing;
} s5l8920_iop_t;

/* Explicit partial ARMv4T profile, not a physical CPU/revision identification.
 * Constructor values do not establish hardware reset state: a real reset
 * pulse is required before register access or execution. No default image.
 * Calls must be serialized between AP and IOP instructions. */
void s5l8920_iop_init(s5l8920_iop_t *i);
/* Assert resets/holds the CPU and invalidates firmware-window programming.
 * Release leaves it held until RUN. Repeated levels preserve state. RAM is
 * untouched. This is a functional reset signal, without physical latency. */
bool s5l8920_iop_reset_line(s5l8920_iop_t *i,bool asserted);
/* Version1 protocol only: +100 control/stop status, +110 physical base,
 * +114 inverse power-of-two length mask, +118 self address. Control0 stops
 * without resetting the CPU; control1 runs/resumes a fully configured RAM
 * window. The original post-reset control10 request is accepted only while
 * held at reset with no firmware programming. Bit1 reflects requested stop,
 * never an unsupported-instruction or memory failure. Other control bits,
 * live remapping and unknown initial register readbacks are refused.
 * Cache registers and IRQ/mailbox/VIC/timer routing remain unavailable. */
bool s5l8920_iop_read(const s5l8920_iop_t *i,uint32_t offset,uint32_t *value);
bool s5l8920_iop_write(s5l8920_iop_t *i,uint32_t offset,uint32_t value,
                     s5l8920_iop_memory_fn memory,void *owner);
/* Execute at most one real instruction. Held/reset states do no work. Bus
 * failure is retained separately from the AP and does not manufacture a
 * stopped acknowledgement. No implicit CPU execution on reads/writes, and
 * no inferred ratio to AP clocks. Unsupported memory and instructions stop
 * at the original PC. Earlier successful stores in a multi-store remain.
 * The low firmware window is the only implemented IOP memory mapping. */
arm_status_t s5l8920_iop_service(s5l8920_iop_t *i,
                               s5l8920_iop_memory_fn memory,void *owner);
#endif
