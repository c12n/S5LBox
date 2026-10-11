/* Actual CPU/shared-RAM execution and bounded IOP controller regressions.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define BASE 0x41000000u
#define REG(n) (S5L8920_IOP_BASE+(n))
#define GATE(n) (S5L8920_CLOCK_GATE_BASE+4u*(n))
#define RESET GATE(S5L8920_IOP_RESET_GATE)
#define OTHER GATE(S5L8920_IOP_DEPENDENCY_GATE)
static s5l8920_t board;
static s5l8920_iop_t iop;
static unsigned passed,failed;
#define CHECK(x) do { if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);} } while(0)
static void wr(uint32_t a,uint32_t v) { board.bus.write32(&board,a,v); }
static uint32_t rd(uint32_t a) { return board.bus.read32(&board,a); }
static void init(void) {
    s5l8920_free(&board);CHECK(s5l8920_init(&board));s5l8920_iop_init(&iop);
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    CHECK(s5l8920_iop_attach(&board,&iop));
}
static void pulse(void) { wr(RESET,0x8000000fu);wr(RESET,15);CHECK(!board.bus_failure.reason); }
static void program(uint32_t base,uint32_t mask) {
    wr(REG(0x100),0x10);wr(REG(0x118),S5L8920_IOP_BASE);
    wr(REG(0x110),base);wr(REG(0x114),mask);CHECK(!board.bus_failure.reason);
}
static void run(void) { wr(REG(0x100),1);CHECK(!board.bus_failure.reason && iop.running); }
static bool bus_stop_preserves_cpu(const arm_cpu_t *before) {
    /* The interpreter retains its internal bus-abort diagnostic. All other
     * state, including registers, banks, CPSR and retirement, must survive. */
    arm_cpu_t after=iop.cpu;
    CHECK(!after.abort_pending && after.abort_fsr==ARM_MMU_BUS_FAILURE &&
          after.abort_far==iop.failure.address);
    after.abort_pending=before->abort_pending;after.abort_fsr=before->abort_fsr;
    after.abort_far=before->abort_far;
    return !memcmp(before,&after,sizeof after);
}
static void refuse(uint32_t a,unsigned size,uint32_t value,s5l8920_bus_reason_t reason) {
    s5l8920_iop_t before=iop;
    if(size==1)board.bus.write8(&board,a,(uint8_t)value);
    else if(size==2)board.bus.write16(&board,a,(uint16_t)value);
    else wr(a,value);
    CHECK(board.bus_failure.reason==reason && board.bus_failure.address==a);
    CHECK(!memcmp(&before,&iop,sizeof iop));
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(!memcmp(&before,&iop,sizeof iop));
    s5l8920_clear_bus_failure(&board);
}
static void test_reset_and_attachment(void) {
    init();CHECK(!iop.reset_seen && !iop.running && iop.cpu.arch==ARM_ARCH_V4T);
    CHECK(s5l8920_iop_attach(&board,&iop));
    s5l8920_iop_t other;s5l8920_iop_init(&other);
    CHECK(!s5l8920_iop_attach(&board,&other));
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    uint32_t untouched=0xaabbccddu;
    CHECK(!s5l8920_iop_read(&iop,0x100,&untouched) && untouched==0xaabbccddu);
    refuse(REG(0x100),4,0x10,S5L8920_BUS_REGISTER_REFUSED);
    CHECK(s5l8920_clock_gate_configure(&board,0x2du,15));
    refuse(GATE(0x2du),4,0x8000000fu,S5L8920_BUS_REGISTER_REFUSED);
    CHECK(rd(GATE(0x2du))==15);
    wr(RESET,0x8000000fu);
    CHECK(iop.reset_asserted && iop.reset_seen && !iop.running);
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.cycles==0);
    refuse(REG(0x100),4,0x10,S5L8920_BUS_REGISTER_REFUSED);
    wr(RESET,15);CHECK(rd(REG(0x100))==2 && !iop.reset_asserted);
    CHECK(!s5l8920_iop_read(&iop,0x110,&untouched) && untouched==0xaabbccddu);
    CHECK(!s5l8920_iop_read(&iop,0x114,&untouched));
    CHECK(!s5l8920_iop_read(&iop,0x118,&untouched));
    refuse(REG(0x100),4,1,S5L8920_BUS_REGISTER_REFUSED);
    program(BASE,0x3ffff000u);run();
    wr(BASE,0xe3a0002au); /* MOV r0,#42 */
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.r[0]==42);
    wr(RESET,0x8000000fu);
    CHECK(iop.cpu.r[15]==0 && iop.cpu.cycles==0 && !iop.programmed && !iop.running);
    CHECK(rd(BASE)==0xe3a0002au); /* Reset does not erase shared RAM. */
    s5l8920_iop_t held=iop;wr(RESET,0x8000000fu);CHECK(!memcmp(&held,&iop,sizeof iop));
    wr(RESET,15);CHECK(rd(REG(0x100))==2);
    CHECK(s5l8920_reset(&board) && !board.iop);
    CHECK(!s5l8920_iop_attach(&board,&iop));
    s5l8920_iop_init(&iop);CHECK(s5l8920_iop_attach(&board,&iop));
    CHECK(!iop.memory && !iop.memory_owner);
    s5l8920_free(&board);CHECK(!board.iop && !board.ram);
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(!iop.memory && !iop.memory_owner);
    CHECK(s5l8920_init(&board));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(!s5l8920_iop_attach(&board,&iop)); /* Missing second gate. */
    refuse(RESET,4,0x8000000fu,S5L8920_BUS_REGISTER_REFUSED);
    refuse(REG(0x100),4,0x10,S5L8920_BUS_UNMAPPED);
}
static void test_shared_execution(void) {
    init();pulse();program(BASE,0x3ffff000u);
    wr(BASE,0xe3a0002au);wr(BASE+4,0xe3a01080u);
    wr(BASE+8,0xe5810000u);wr(BASE+12,0xe5912000u); /* STR/LDR r0/r2,[r1] */
    wr(BASE+16,0xe12fff13u); /* BX r3 */
    run();arm_cpu_t ap=board.cpu;
    for(unsigned n=0;n<2;++n)CHECK(s5l8920_iop_board_service(&board)==ARM_OK);
    CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && iop.cpu.r[15]==8 && iop.cpu.cycles==2);
    wr(REG(0x100),0);CHECK(rd(REG(0x100))==2);
    s5l8920_iop_t paused=iop;
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && !memcmp(&paused,&iop,sizeof iop));
    run();CHECK(iop.cpu.r[15]==8 && iop.cpu.r[0]==42);
    board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK);
    CHECK(rd(BASE+128)==42 && iop.bytes_written==4);
    CHECK(!board.cpu.excl_valid && !board.cpu.a8_excl_size);
    wr(BASE+128,0xdeadbeefu); /* AP write is immediately visible to real IOP LDR. */
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.r[2]==0xdeadbeefu);
    CHECK(!iop.memory && !iop.memory_owner && !iop.servicing);
    refuse(REG(0x110),4,BASE+4096,S5L8920_BUS_REGISTER_REFUSED);
    for(unsigned n=0;n<2;++n) {
        uint32_t gate=n?OTHER:RESET;wr(gate,0);paused=iop;
        CHECK(s5l8920_iop_board_service(&board)==ARM_OK && !memcmp(&paused,&iop,sizeof iop));
        refuse(REG(0x100),4,0,S5L8920_BUS_REGISTER_REFUSED);
        wr(gate,15);CHECK(rd(REG(0x100))==1);
    }
    iop.cpu.r[3]=BASE; /* A physical DRAM alias is not an IOP low-window alias. */
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.r[15]==BASE);
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(iop.failure.reason==S5L8920_IOP_UNMAPPED && iop.failure.address==BASE);
    CHECK(!board.bus_failure.reason && rd(REG(0x100))==1 && iop.running);
}
static void test_failures_and_partial_store(void) {
    init();pulse();program(BASE,0x3ffff000u);wr(BASE,0xe5801000u);run();
    iop.cpu.r[0]=0x86300020u;iop.cpu.r[1]=1;
    arm_cpu_t before=iop.cpu;
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(bus_stop_preserves_cpu(&before) && !iop.bytes_written);
    CHECK(iop.failure.reason==S5L8920_IOP_UNMAPPED && iop.failure.address==0x86300020u &&
          iop.failure.pc==0 && iop.failure.write && iop.failure.value==1 && iop.failure.size==4);
    CHECK(!board.bus_failure.reason && rd(REG(0x100))==1);
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT && bus_stop_preserves_cpu(&before));
    wr(REG(0x100),0);CHECK(rd(REG(0x100))==2);
    refuse(REG(0x100),4,1,S5L8920_BUS_REGISTER_REFUSED);
    pulse();program(BASE,0x3ffff000u);wr(BASE,0xee100f10u);run(); /* CP15 MRC unsupported */
    before=iop.cpu;CHECK(s5l8920_iop_board_service(&board)==ARM_UNDEFINED);
    CHECK(!memcmp(&before,&iop.cpu,sizeof before) && !iop.failure.reason && rd(REG(0x100))==1);
    pulse();program(BASE,0x3ffff000u);wr(BASE,0xe8a1000du);run(); /* STMIA r1!,{r0,r2,r3} */
    iop.cpu.r[0]=0x11223344u;iop.cpu.r[1]=4092;iop.cpu.r[2]=0xaabbccddu;
    before=iop.cpu;board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(bus_stop_preserves_cpu(&before));
    CHECK(rd(BASE+4092)==0x11223344u && iop.bytes_written==4);
    CHECK(!board.cpu.excl_valid && !board.cpu.a8_excl_size);
    CHECK(iop.failure.address==4096 && iop.failure.write && rd(REG(0x100))==1);
}
static void test_programming_bounds(void) {
    init();pulse();
    const uint32_t controls[]={2,3,4,8,0x11,0x80000000u,0xffffffffu};
    for(unsigned n=0;n<sizeof controls/sizeof controls[0];++n)
        refuse(REG(0x100),4,controls[n],S5L8920_BUS_REGISTER_REFUSED);
    const uint32_t masks[]={1,0x3fffffffu,0xfffff000u,0x3fffa000u,0x80000000u};
    for(unsigned n=0;n<sizeof masks/sizeof masks[0];++n)
        refuse(REG(0x114),4,masks[n],S5L8920_BUS_REGISTER_REFUSED);
    refuse(REG(0x110),4,BASE+1,S5L8920_BUS_REGISTER_REFUSED);
    refuse(REG(0x118),4,S5L8920_IOP_BASE+4096,S5L8920_BUS_REGISTER_REFUSED);
    refuse(REG(0x104),4,0,S5L8920_BUS_REGISTER_REFUSED);
    refuse(REG(0x108),4,0,S5L8920_BUS_REGISTER_REFUSED);
    refuse(REG(0x100),1,0,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(REG(0x100),2,0,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(REG(0x101),4,0,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(REG(0x20),4,1,S5L8920_BUS_UNMAPPED);
    refuse(RESET,4,0x80000001u,S5L8920_BUS_REGISTER_REFUSED);
    CHECK(rd(RESET)==15 && !iop.reset_asserted);
    refuse(RESET,4,0x4000000fu,S5L8920_BUS_REGISTER_REFUSED);
    program(0xfffff000u,0x3fffe000u); /* Physical range wraps. */
    refuse(REG(0x100),4,1,S5L8920_BUS_REGISTER_REFUSED);
    wr(REG(0x110),S5L8920_RAM_BASE+S5L8920_RAM_SIZE-4096);
    refuse(REG(0x100),4,1,S5L8920_BUS_REGISTER_REFUSED); /* Beyond allocated DRAM. */
    wr(REG(0x110),0);refuse(REG(0x100),4,1,S5L8920_BUS_REGISTER_REFUSED);
    wr(REG(0x110),BASE);wr(REG(0x114),0); /* One GiB exceeds physical memory. */
    refuse(REG(0x100),4,1,S5L8920_BUS_REGISTER_REFUSED);
    wr(REG(0x114),0x3ffff000u);run();
    CHECK(rd(REG(0x110))==BASE && rd(REG(0x114))==0x3ffff000u && rd(REG(0x118))==S5L8920_IOP_BASE);
    wr(REG(0x100),0);refuse(REG(0x100),4,0x10,S5L8920_BUS_REGISTER_REFUSED);
}
int main(void) {
    test_reset_and_attachment();test_shared_execution();
    test_failures_and_partial_store();test_programming_bounds();
    s5l8920_free(&board);
    printf("s5l8920_iop: %u passed, %u failed\n",passed,failed);
    return failed?1:0;
}
