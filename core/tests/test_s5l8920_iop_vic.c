/* AP software interrupts, real IOP exception entry/return, masks and EOI.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define RAM 0x41000000u
#define VIC(n,r) (S5L8920_IOP_VIC_BASE+(n)*S5L8920_IOP_VIC_STRIDE+(r))
#define CTRL (S5L8920_IOP_BASE+0x100u)
#define RESET (S5L8920_CLOCK_GATE_BASE+4*S5L8920_IOP_RESET_GATE)
#define CLOCK (S5L8920_CLOCK_GATE_BASE+4*S5L8920_IOP_DEPENDENCY_GATE)
static s5l8920_t board;
static s5l8920_iop_t iop;
static unsigned passed,failed;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);}} while(0)
static void wr(uint32_t a,uint32_t v) {board.bus.write32(&board,a,v);}
static uint32_t rd(uint32_t a) {return board.bus.read32(&board,a);}
static void init(bool vic) {
    s5l8920_free(&board);CHECK(s5l8920_init(&board));s5l8920_iop_init(&iop);
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    if(vic)CHECK(s5l8920_iop_vic_configure(&iop));
    CHECK(s5l8920_iop_attach(&board,&iop));
    wr(RESET,0x8000000fu);wr(RESET,15);CHECK(!board.bus_failure.reason);
}
static void run(void) {
    wr(S5L8920_IOP_BASE+0x118,S5L8920_IOP_BASE);
    wr(S5L8920_IOP_BASE+0x110,RAM);wr(S5L8920_IOP_BASE+0x114,0x3ffff000u);
    wr(CTRL,1);CHECK(!board.bus_failure.reason && iop.running);
}
static void step(unsigned n) {while(n--)CHECK(s5l8920_iop_board_service(&board)==ARM_OK);}
static void refuse(uint32_t address,unsigned size,uint32_t value,s5l8920_bus_reason_t reason) {
    s5l8920_iop_t before=iop;
    if(size==1)board.bus.write8(&board,address,(uint8_t)value);
    else if(size==2)board.bus.write16(&board,address,(uint16_t)value);
    else wr(address,value);
    CHECK(board.bus_failure.reason==reason && board.bus_failure.address==address);
    CHECK(!memcmp(&before,&iop,sizeof iop));s5l8920_clear_bus_failure(&board);
}
static void test_pending_and_banks(void) {
    init(true);CHECK(!iop.cpu.irq_line && !iop.cpu.fiq_line);
    for(unsigned b=0;b<3;b++) {
        CHECK(rd(VIC(b,PL192_RAWINTR))==0 && rd(VIC(b,PL192_INTENABLE))==0);
        wr(VIC(b,PL192_SOFTINT),8);wr(VIC(b,PL192_SOFTINT),0);
        CHECK(rd(VIC(b,PL192_RAWINTR))==8 && rd(VIC(b,PL192_SOFTINT))==8);
        CHECK(rd(VIC(b,PL192_IRQSTATUS))==0 && !iop.cpu.irq_line);
        wr(VIC(b,PL192_INTENABLE),8);CHECK(iop.cpu.irq_line);
        wr(VIC(b,PL192_VECTADDR0+12),32*b+3);
        CHECK(rd(VIC(b,PL192_ADDRESS))==32*b+3 && !iop.cpu.irq_line);
        CHECK(rd(VIC(b,PL192_SOFTINT))==8); /* Acknowledge does not clear source. */
        wr(VIC(b,PL192_ADDRESS),1);CHECK(iop.cpu.irq_line);
        wr(VIC(b,PL192_SOFTINTCLEAR),8);CHECK(!iop.cpu.irq_line && rd(VIC(b,PL192_RAWINTR))==0);
    }
    wr(VIC(1,PL192_SOFTINT),8);wr(VIC(2,PL192_SOFTINT),8);
    wr(VIC(1,PL192_SWPRIORITYMASK),0);CHECK(iop.cpu.irq_line);
    wr(VIC(2,PL192_INTENCLEAR),8);CHECK(!iop.cpu.irq_line);
    CHECK(rd(VIC(2,PL192_SOFTINT))==8);
    wr(VIC(1,PL192_SWPRIORITYMASK),0xffff);CHECK(iop.cpu.irq_line);
    wr(VIC(1,PL192_INTSELECT),8);CHECK(!iop.cpu.irq_line && iop.cpu.fiq_line);
    wr(VIC(1,PL192_SOFTINTCLEAR),8);CHECK(!iop.cpu.fiq_line);
    CHECK(!board.cpu.irq_line && !board.cpu.fiq_line && !board.bus_failure.reason);
}
static void program_handlers(void) {
    wr(RAM,0xe321f013u); /* MSR CPSR_c,#SVC: actually unmask IRQ/FIQ. */
    wr(RAM+4,0xeafffffeu);wr(RAM+0x18,0xea000008u);wr(RAM+0x1c,0xea00001fu);
    /* IRQ: acknowledge vector, clear software source3, EOI, count, return. */
    wr(RAM+0x40,0xe59f0038u);wr(RAM+0x44,0xe5902f00u);wr(RAM+0x48,0xe3a01008u);
    wr(RAM+0x4c,0xe580101cu);wr(RAM+0x50,0xe3a01001u);wr(RAM+0x54,0xe5801f00u);
    wr(RAM+0x58,0xe2833001u);wr(RAM+0x5c,0xe25ef004u);wr(RAM+0x80,S5L8920_IOP_VIC_BASE);
    /* FIQ: clear software source2, count, exception return. */
    wr(RAM+0xa0,0xe59f0010u);wr(RAM+0xa4,0xe3a01004u);wr(RAM+0xa8,0xe580101cu);
    wr(RAM+0xac,0xe2844001u);wr(RAM+0xb0,0xe25ef004u);wr(RAM+0xb8,S5L8920_IOP_VIC_BASE);
}
static void test_actual_exception_delivery(void) {
    init(true);program_handlers();run();
    wr(VIC(0,PL192_VECTADDR0+12),0x12345678u);wr(VIC(0,PL192_SOFTINT),8);
    step(1);CHECK(iop.cpu.r[15]==4 && iop.cpu.cpsr==ARM_MODE_SVC && !iop.cpu.irq_line);
    wr(VIC(0,PL192_INTENABLE),8);CHECK(iop.cpu.irq_line);
    board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;arm_cpu_t ap=board.cpu;
    step(1);CHECK(iop.cpu.r[15]==0x18 && (iop.cpu.cpsr&31u)==ARM_MODE_IRQ && iop.cpu.r[14]==8);
    step(3);CHECK(iop.cpu.r[15]==0x48 && iop.cpu.r[2]==0x12345678u && !iop.cpu.irq_line);
    CHECK(iop.vic[0].in_service==0x8000u && iop.vic[0].soft==8);
    step(6);CHECK(iop.cpu.r[15]==4 && iop.cpu.r[3]==1 && iop.cpu.cpsr==ARM_MODE_SVC);
    CHECK(!iop.vic[0].soft && !iop.vic[0].in_service && !iop.cpu.irq_line);
    CHECK(!iop.bytes_written && !memcmp(&ap,&board.cpu,sizeof ap));
    /* Both requests pending: FIQ outranks IRQ, then the IRQ survives FIQ. */
    wr(VIC(0,PL192_INTSELECT),4);wr(VIC(0,PL192_INTENABLE),4);wr(VIC(0,PL192_SOFTINT),12);
    step(1);CHECK(iop.cpu.r[15]==0x1c && (iop.cpu.cpsr&31u)==ARM_MODE_FIQ);
    step(6);CHECK(iop.cpu.r[15]==4 && iop.cpu.r[4]==1 && iop.cpu.irq_line && !iop.cpu.fiq_line);
    step(1);CHECK(iop.cpu.r[15]==0x18 && (iop.cpu.cpsr&31u)==ARM_MODE_IRQ);
    step(9);CHECK(iop.cpu.r[15]==4 && iop.cpu.r[3]==2 && !iop.cpu.irq_line && !iop.cpu.fiq_line);
    CHECK(!board.bus_failure.reason && !iop.failure.reason && !memcmp(&ap,&board.cpu,sizeof ap));
}
static void test_reset_and_refusal(void) {
    init(false);CHECK(!s5l8920_iop_vic_configure(&iop)); /* Cold configuration only. */
    refuse(VIC(0,PL192_SOFTINT),4,8,S5L8920_BUS_UNMAPPED);
    init(true);wr(VIC(0,PL192_INTENABLE),8);wr(VIC(0,PL192_SOFTINT),8);
    s5l8920_iop_t before=iop;CHECK(s5l8920_iop_vic_configure(&iop));CHECK(!memcmp(&before,&iop,sizeof iop));
    wr(RESET,0x8000000fu);CHECK(iop.vic[0].soft==8 && iop.vic[0].enable==8 && iop.cpu.irq_line);
    refuse(VIC(0,PL192_SOFTINTCLEAR),4,8,S5L8920_BUS_REGISTER_REFUSED);
    wr(RESET,15);CHECK(rd(VIC(0,PL192_SOFTINT))==8);
    wr(CLOCK,0);refuse(VIC(0,PL192_SOFTINTCLEAR),4,8,S5L8920_BUS_REGISTER_REFUSED);wr(CLOCK,15);
    refuse(VIC(0,PL192_SOFTINT),1,8,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(VIC(0,PL192_SOFTINT),2,8,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(VIC(0,PL192_SOFTINT+1),4,8,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(VIC(0,PL192_PROTECTION),4,1,S5L8920_BUS_REGISTER_REFUSED);
    refuse(VIC(0,PL192_SWPRIORITYMASK),4,0x10000,S5L8920_BUS_REGISTER_REFUSED);
    refuse(VIC(0,0x30),4,0,S5L8920_BUS_REGISTER_REFUSED);
    refuse(VIC(0,0x1000),4,0,S5L8920_BUS_UNMAPPED);
    refuse(VIC(3,0),4,0,S5L8920_BUS_UNMAPPED);
    uint32_t value=0xaabbccddu;
    CHECK(!s5l8920_iop_vic_read(&iop,0xfe0,&value) && value==0xaabbccddu);
    CHECK(!s5l8920_iop_vic_read(&iop,0x20,&value) && value==0xaabbccddu);
    /* Refused IOP register access diagnoses the IOP, not the AP. */
    wr(RAM,0xe5901000u);run();iop.cpu.r[0]=VIC(0,0xfe0);
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED && iop.failure.address==VIC(0,0xfe0));
    CHECK(iop.cpu.r[15]==0 && iop.cpu.cycles==0 && !board.bus_failure.reason && rd(CTRL)==1);
    CHECK(s5l8920_reset(&board) && !board.iop);
    s5l8920_iop_init(&iop);CHECK(!iop.vic_configured && !iop.cpu.irq_line && !iop.cpu.fiq_line);
}
int main(void) {
    test_pending_and_banks();test_actual_exception_delivery();test_reset_and_refusal();
    s5l8920_free(&board);printf("s5l8920_iop_vic: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
