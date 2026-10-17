/* Actual supplied time, independent countdown and latched IOP interrupts.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define RAM 0x41000000u
#define LOW 0xbf100200u
#define HIGH 0xbf100204u
#define COUNT 0xbf10020cu
#define CONTROL 0xbf100224u
#define CTRL (S5L8920_IOP_BASE+0x100u)
#define RESET (S5L8920_CLOCK_GATE_BASE+4*S5L8920_IOP_RESET_GATE)
static s5l8920_t board;
static s5l8920_iop_t iop;
static unsigned passed,failed;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);}} while(0)
static void wr(uint32_t a,uint32_t v){board.bus.write32(&board,a,v);}
static void init(bool selected){
    s5l8920_free(&board);CHECK(s5l8920_init(&board));s5l8920_iop_init(&iop);
    CHECK(s5l8920_iop_vic_configure(&iop));
    if(selected)CHECK(s5l8920_iop_timer_configure(&iop));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    CHECK(s5l8920_iop_attach(&board,&iop));wr(RESET,0x8000000fu);wr(RESET,15);
    wr(RAM,0xe5801000u);wr(RAM+4,0xe5901000u); /* STR/LDR r1,[r0]. */
    wr(RAM+8,0xe5c01000u);wr(RAM+12,0xe5d01000u); /* STRB/LDRB. */
    wr(RAM+16,0xe1c010b0u);wr(RAM+20,0xe1d010b0u); /* STRH/LDRH. */
    wr(S5L8920_IOP_BASE+0x118,S5L8920_IOP_BASE);wr(S5L8920_IOP_BASE+0x110,RAM);
    wr(S5L8920_IOP_BASE+0x114,0x3ffff000u);wr(CTRL,1);
    CHECK(!board.bus_failure.reason && iop.running);
}
static arm_status_t execute(unsigned pc,uint32_t address,uint32_t value){
    iop.cpu.r[15]=pc;iop.cpu.r[0]=address;iop.cpu.r[1]=value;
    return s5l8920_iop_board_service(&board);
}
static void command(uint32_t address,uint32_t value){CHECK(execute(0,address,value)==ARM_OK);}
static uint32_t read_time(uint32_t address){CHECK(execute(4,address,0xdeadbeef)==ARM_OK);return iop.cpu.r[1];}
static uint8_t *memory(void *owner,uint32_t address,uint32_t size){
    s5l8920_t *m=owner;
    if(address<RAM || size>0x1000 || address-RAM>0x1000-size)return NULL;
    return m->ram+address-S5L8920_RAM_BASE;
}
static uint64_t reentry_timebase(void *owner){
    s5l8920_t *m=owner;uint64_t before=m->timebase_ticks;
    s5l8920_iop_timer_t timer=iop.timer;
    CHECK(!s5l8920_timebase_clock(m,1));CHECK(!s5l8920_iop_timer_clock(&iop,1));
    CHECK(m->timebase_ticks==before && !memcmp(&timer,&iop.timer,sizeof timer));
    CHECK(!s5l8920_iop_timer_configure(&iop));return before;
}
static void test_timebase(void){
    init(true);CHECK(s5l8920_timebase_clock(&board,UINT64_C(0x12345678fffffffe)));
    CHECK(read_time(LOW)==0xfffffffe && read_time(HIGH)==0x12345678);
    CHECK(board.timebase_ticks==UINT64_C(0x12345678fffffffe));
    CHECK(s5l8920_timebase_clock(&board,3));
    CHECK(read_time(LOW)==1 && read_time(HIGH)==0x12345679);
    CHECK(s5l8920_timebase_clock(&board,UINT64_MAX-board.timebase_ticks+1));
    CHECK(read_time(LOW)==0 && read_time(HIGH)==0);
    CHECK(!iop.timebase && !iop.memory && !iop.memory_owner && !iop.signal_ap && !iop.bytes_written);
    command(COUNT,7);command(CONTROL,3);iop.cpu.r[15]=4;iop.cpu.r[0]=LOW;
    CHECK(s5l8920_iop_service_with_timebase(&iop,memory,NULL,reentry_timebase,&board)==ARM_OK);
    CHECK(iop.timer.remaining==7 && !iop.timebase && !iop.memory && !iop.memory_owner);
    init(true);iop.cpu.r[15]=4;iop.cpu.r[0]=LOW;iop.cpu.r[1]=0xdeadbeef;
    CHECK(s5l8920_iop_service(&iop,memory,&board)==ARM_HALT);
    CHECK(iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED && iop.cpu.r[1]==0xdeadbeef);
    CHECK(!board.bus_failure.reason && !iop.timebase && !iop.memory && !iop.memory_owner);
}
static void test_deadline(void){
    init(true);board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    command(COUNT,5);command(CONTROL,3);
    CHECK(board.cpu.excl_valid && board.cpu.a8_excl_size==4 && !iop.bytes_written);
    wr(S5L8920_PMGR_BASE+S5L8920_DEADLINE_COUNT,7);
    wr(S5L8920_PMGR_BASE+S5L8920_DEADLINE_CONTROL,3);
    CHECK(s5l8920_timebase_clock(&board,4));
    CHECK(iop.timer.remaining==1 && !iop.timer.pending && board.deadline.remaining==3);
    CHECK(s5l8920_timebase_clock(&board,1));
    CHECK(iop.timer.pending && iop.timer.expired && !iop.timer.remaining && !iop.cpu.irq_line);
    CHECK(board.deadline.remaining==2 && !board.deadline.pending && !board.cpu.irq_line);
    wr(S5L8920_IOP_VIC_BASE+PL192_INTENABLE,32);CHECK(iop.cpu.irq_line && !board.cpu.irq_line);
    CHECK(s5l8920_timebase_clock(&board,2) && board.deadline.pending && iop.timer.pending);
    command(CONTROL,1);CHECK(iop.timer.pending);command(CONTROL,0);CHECK(iop.timer.pending);
    command(COUNT,9);CHECK(!iop.timer.expired && iop.timer.pending && !iop.timer.enabled);
    command(CONTROL,2);CHECK(!iop.timer.pending && !iop.cpu.irq_line && board.deadline.pending);
    command(CONTROL,3);CHECK(s5l8920_timebase_clock(&board,9) && iop.timer.pending);
    wr(S5L8920_IOP_VIC_BASE+PL192_SOFTINT,32);command(CONTROL,2);
    CHECK(!iop.timer.pending && iop.vic[0].soft==32 && iop.cpu.irq_line);
    wr(S5L8920_IOP_VIC_BASE+PL192_SOFTINTCLEAR,32);CHECK(!iop.cpu.irq_line);
    wr(S5L8920_IOP_VIC_BASE+PL192_INTSELECT,32);command(COUNT,1);command(CONTROL,3);
    CHECK(s5l8920_timebase_clock(&board,1) && iop.cpu.fiq_line && !iop.cpu.irq_line);
    command(CONTROL,2);CHECK(!iop.cpu.fiq_line && !iop.cpu.irq_line);
    command(CONTROL,1);CHECK(s5l8920_timebase_clock(&board,100) && !iop.timer.pending);
    command(COUNT,0);CHECK(s5l8920_timebase_clock(&board,0) && !iop.timer.pending);
    CHECK(s5l8920_timebase_clock(&board,1) && iop.timer.pending);
    command(CONTROL,2);command(COUNT,UINT32_MAX);command(CONTROL,3);
    CHECK(s5l8920_timebase_clock(&board,UINT64_C(0x100000000)) && iop.timer.expired && iop.timer.pending);
}
static void test_lifetime(void){
    init(true);command(COUNT,2);command(CONTROL,3);
    s5l8920_iop_timer_t before=iop.timer;
    CHECK(s5l8920_iop_timer_configure(&iop) && !memcmp(&before,&iop.timer,sizeof before));
    wr(CTRL,0);uint64_t cycles=iop.cpu.cycles;
    CHECK(s5l8920_timebase_clock(&board,2) && iop.timer.pending);
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.cycles==cycles);
    before=iop.timer;wr(RESET,0x8000000fu);
    CHECK(!board.bus_failure.reason && iop.reset_asserted && !memcmp(&before,&iop.timer,sizeof before));
    wr(RESET,15);CHECK(!memcmp(&before,&iop.timer,sizeof before));
    s5l8920_iop_init(&iop);CHECK(!iop.timer.configured && !iop.timer.pending && !iop.timebase);
    init(true);command(COUNT,1);command(CONTROL,3);wr(RESET,0);
    cycles=iop.cpu.cycles;CHECK(s5l8920_timebase_clock(&board,1) && iop.timer.pending);
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.cycles==cycles);
    init(false);CHECK(!s5l8920_iop_timer_configure(&iop));CHECK(!s5l8920_iop_timer_clock(&iop,1));
    CHECK(execute(0,CONTROL,2)==ARM_HALT && iop.failure.reason==S5L8920_IOP_UNMAPPED);
    s5l8920_iop_init(&iop);CHECK(!s5l8920_iop_timer_configure(&iop));
    CHECK(!s5l8920_iop_timer_configure(NULL) && !s5l8920_iop_timer_clock(NULL,1));
}
static void test_refusals(void){
    const struct {unsigned pc;uint32_t address,value;} bad[]={
        {0,CONTROL,1},{0,CONTROL,4},{4,COUNT,0},{4,CONTROL,0},
        {0,0xbf100208,1},{0,0xbf100220,2},{0,LOW,0},{0,HIGH,0},
        {8,CONTROL,2},{16,COUNT,1},{12,HIGH,0},{20,LOW,0},{8,CONTROL+1,2}
    };
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];n++){
        init(true);s5l8920_iop_timer_t before=iop.timer;
        arm_status_t result=execute(bad[n].pc,bad[n].address,bad[n].value);
        /* The partial ARMv4T decoder stops these halfword instructions before
         * any device access. Keep that CPU limit separate from MMIO refusal. */
        bool halfword=bad[n].pc==16 || bad[n].pc==20;
        arm_status_t expected=halfword?ARM_UNDEFINED:ARM_HALT;
        s5l8920_iop_failure_reason_t reason=halfword?S5L8920_IOP_NO_FAILURE:S5L8920_IOP_REGISTER_REFUSED;
        if(result!=expected || iop.failure.reason!=reason)
            printf("REFUSAL case=%u instruction=%08x address=%08x status=%u reason=%u\n",n,bad[n].pc,bad[n].address,result,iop.failure.reason);
        CHECK(result==expected);
        CHECK(iop.failure.reason==reason && !memcmp(&before,&iop.timer,sizeof before));
        CHECK(!board.bus_failure.reason && !iop.bytes_written && iop.running);
        CHECK(!iop.timebase && !iop.memory && !iop.signal_ap && !iop.memory_owner && !iop.servicing);
    }
}
int main(void){
    test_timebase();test_deadline();test_lifetime();test_refusals();s5l8920_free(&board);
    printf("s5l8920_iop_timer: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
