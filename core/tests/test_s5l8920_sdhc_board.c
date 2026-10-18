/* Shared SDHC state, clock guards, and AP/IOP fault isolation.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define CODE UINT32_C(0x41000000)
#define REG(n) (S5L8920_SDHC_BASE+(n))
#define GATE (S5L8920_CLOCK_GATE_BASE+4u*S5L8920_SDHC_GATE)
static s5l8920_t board;
static s5l8920_iop_t iop;
static s5l8920_sdhc_t sdhc,other;
static unsigned passed,failed;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);}}while(0)
static void wr(uint32_t a,uint32_t v) {board.bus.write32(&board,a,v);}
static void init(bool attached) {
    s5l8920_free(&board);CHECK(s5l8920_init(&board));
    sdhc=(s5l8920_sdhc_t){0};other=(s5l8920_sdhc_t){0};
    CHECK(s5l8920_sdhc_init(&sdhc) && s5l8920_sdhc_init(&other));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_SDHC_GATE,15));
    if(attached)CHECK(s5l8920_sdhc_attach(&board,&sdhc));
    s5l8920_iop_init(&iop);
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    CHECK(s5l8920_iop_attach(&board,&iop));
    wr(S5L8920_CLOCK_GATE_BASE+4u*S5L8920_IOP_RESET_GATE,0x8000000f);
    wr(S5L8920_CLOCK_GATE_BASE+4u*S5L8920_IOP_RESET_GATE,15);
    wr(S5L8920_IOP_BASE+0x110,CODE);wr(S5L8920_IOP_BASE+0x114,0x3ffff000);
    wr(S5L8920_IOP_BASE+0x118,S5L8920_IOP_BASE);wr(S5L8920_IOP_BASE+0x100,1);
    CHECK(!board.bus_failure.reason && iop.running);
}
static void instruction(uint32_t opcode,uint32_t address,uint32_t value) {
    wr(CODE,opcode);iop.cpu.r[15]=0;iop.cpu.r[0]=address;iop.cpu.r[1]=value;
}
static arm_status_t step(void) {
    arm_status_t status=s5l8920_iop_board_service(&board);
    CHECK(!iop.servicing && !iop.memory && !iop.sdhc && !iop.fmi && !iop.memory_owner);
    return status;
}
static void test_shared_state(void) {
    init(true);board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    const struct {uint32_t store,load,offset,value;unsigned width;} cases[]={
        {0xe5801000,0xe5901000,8,0x12345678,4},
        {0xe1c010b0,0xe1d010b0,6,13,2},
        {0xe5c01000,0xe5d01000,0x28,3,1}
    };
    for(unsigned n=0;n<3;n++) {
        uint32_t a=REG(cases[n].offset),v=cases[n].value;arm_cpu_t ap=board.cpu;
        instruction(cases[n].store,a,v);CHECK(step()==ARM_OK && !iop.bytes_written);
        uint32_t got=cases[n].width==4?board.bus.read32(&board,a):
            cases[n].width==2?board.bus.read16(&board,a):board.bus.read8(&board,a);
        CHECK(got==v && !memcmp(&ap,&board.cpu,sizeof ap));
        if(cases[n].width==4)wr(a,v+1);
        else if(cases[n].width==2)board.bus.write16(&board,a,(uint16_t)(v+1));
        else board.bus.write8(&board,a,(uint8_t)(v-1));
        instruction(cases[n].load,a,0xdeadbeef);CHECK(step()==ARM_OK);
        CHECK(iop.cpu.r[1]==(cases[n].width==1?v-1:v+1));
        CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason);
    }
    wr(REG(8),0x12345678);instruction(0xe5c01000,REG(0x2f),7);
    CHECK(step()==ARM_OK && sdhc.reset_pending==7 && sdhc.argument==0x12345678);
    s5l8920_sdhc_t pending=sdhc;uint64_t ticks=board.timebase_ticks;
    for(unsigned n=0;n<3;n++) {
        CHECK(board.bus.read8(&board,REG(0x2f))==7);
        instruction(0xe5d01000,REG(0x2f),0);CHECK(step()==ARM_OK && iop.cpu.r[1]==7);
        CHECK(s5l8920_sdhc_board_service(&board,false));
        CHECK(!memcmp(&sdhc,&pending,sizeof sdhc));
    }
    wr(GATE,0);CHECK(s5l8920_sdhc_board_service(&board,true));
    CHECK(!memcmp(&sdhc,&pending,sizeof sdhc));
    wr(GATE,15);arm_cpu_t ap=board.cpu,peer=iop.cpu;
    CHECK(s5l8920_sdhc_board_service(&board,true) && !sdhc.reset_pending && !sdhc.argument);
    CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !memcmp(&peer,&iop.cpu,sizeof peer));
    CHECK(board.timebase_ticks==ticks && !iop.bytes_written && !board.bus_failure.reason);
}
static void test_iop_refusals(void) {
    for(unsigned condition=0;condition<8;condition++) {
        init(condition!=0);
        uint32_t address=REG(0x2f),opcode=0xe5d01000,value=0xdeadbeef;
        if(condition==1)wr(GATE,0);
        if(condition==2)board.clock_gate[S5L8920_SDHC_GATE].configured=false;
        if(condition==3)board.clock_gate[S5L8920_SDHC_GATE].value=0x8000000f;
        if(condition==4)address=REG(0x2c); /* No clock-stable register fabrication. */
        if(condition==5)opcode=0xe5901000; /* Word crossing reset byte is unsupported. */
        if(condition==6) {opcode=0xe5c01000;value=8;}
        if(condition==7)address=REG(0x1000);
        instruction(opcode,address,value);arm_cpu_t ap=board.cpu,cpu=iop.cpu;
        s5l8920_sdhc_t before=sdhc;
        CHECK(step()==ARM_HALT && !memcmp(&ap,&board.cpu,sizeof ap));
        CHECK(!memcmp(cpu.r,iop.cpu.r,sizeof cpu.r) && cpu.cpsr==iop.cpu.cpsr && cpu.cycles==iop.cpu.cycles);
        CHECK(!memcmp(&before,&sdhc,sizeof sdhc) && !board.bus_failure.reason && !iop.bytes_written);
        /* ARM word loads align before bus access; retain the effective address. */
        CHECK(iop.failure.address==(condition==5?(address&~3u):address));
        CHECK(iop.failure.reason==((condition==0 || condition==7)?S5L8920_IOP_UNMAPPED:S5L8920_IOP_REGISTER_REFUSED));
        s5l8920_iop_t stopped=iop;CHECK(step()==ARM_HALT && !memcmp(&stopped,&iop,sizeof iop));
    }
}
static void test_ap_refusals(void) {
    const struct {uint32_t off,value;unsigned size;bool write;} bad[]={
        {0x2f,0,2,false},{0x2f,0,4,false},{0x2c,0,2,false},
        {0x40,0,4,false},{0x0e,1,2,true},{0x2f,8,1,true},{0xfff,0,4,true}
    };
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];n++) {
        init(true);s5l8920_sdhc_t before=sdhc;arm_cpu_t cpu=iop.cpu;
        uint32_t a=REG(bad[n].off);
        if(bad[n].write) {
            if(bad[n].size==1)board.bus.write8(&board,a,(uint8_t)bad[n].value);
            else if(bad[n].size==2)board.bus.write16(&board,a,(uint16_t)bad[n].value);
            else wr(a,bad[n].value);
        }else {
            if(bad[n].size==2)(void)board.bus.read16(&board,a);
            else (void)board.bus.read32(&board,a);
        }
        CHECK(board.bus_failure.reason && board.bus_failure.address==a && board.bus_failure.size==bad[n].size);
        CHECK(!memcmp(&sdhc,&before,sizeof sdhc) && !memcmp(&cpu,&iop.cpu,sizeof cpu));
        s5l8920_bus_failure_t failure=board.bus_failure;
        CHECK(!s5l8920_sdhc_board_service(&board,true) && step()==ARM_HALT);
        CHECK(!memcmp(&failure,&board.bus_failure,sizeof failure));
    }
    for(unsigned condition=0;condition<3;condition++) {
        init(condition!=0);
        if(condition==1)wr(GATE,0);
        if(condition==2)board.clock_gate[S5L8920_SDHC_GATE].configured=false;
        s5l8920_sdhc_t before=sdhc;(void)board.bus.read8(&board,REG(0x2f));
        CHECK(board.bus_failure.reason==(condition==0?S5L8920_BUS_UNMAPPED:S5L8920_BUS_REGISTER_REFUSED));
        CHECK(!memcmp(&before,&sdhc,sizeof sdhc));
    }
}
static void test_lifetime(void) {
    init(false);CHECK(!s5l8920_sdhc_attach(NULL,&sdhc) && !s5l8920_sdhc_attach(&board,NULL));
    CHECK(!s5l8920_sdhc_board_service(NULL,true) && !s5l8920_sdhc_board_service(&board,true));
    sdhc.initialized=false;CHECK(!s5l8920_sdhc_attach(&board,&sdhc));sdhc.initialized=true;
    board.clock_gate[S5L8920_SDHC_GATE].configured=false;CHECK(!s5l8920_sdhc_attach(&board,&sdhc));
    board.clock_gate[S5L8920_SDHC_GATE].configured=true;
    CHECK(s5l8920_sdhc_write(&sdhc,0x2f,1,1) && !s5l8920_sdhc_attach(&board,&sdhc));
    CHECK(s5l8920_sdhc_service(&sdhc,true) && s5l8920_sdhc_attach(&board,&sdhc));
    CHECK(s5l8920_sdhc_attach(&board,&sdhc) && !s5l8920_sdhc_attach(&board,&other));
    board.bus.write8(&board,REG(0x2f),1);s5l8920_sdhc_t pending=sdhc;
    CHECK(s5l8920_sdhc_attach(&board,&sdhc) && !memcmp(&pending,&sdhc,sizeof sdhc));
    wr(S5L8920_IOP_BASE+0x100,0);CHECK(step()==ARM_OK && !memcmp(&pending,&sdhc,sizeof sdhc));
    wr(S5L8920_CLOCK_GATE_BASE+4u*S5L8920_IOP_RESET_GATE,0x8000000f);
    CHECK(!memcmp(&pending,&sdhc,sizeof sdhc));
    CHECK(s5l8920_reset(&board) && !board.sdhc && !memcmp(&pending,&sdhc,sizeof sdhc));
    CHECK(s5l8920_sdhc_service(&sdhc,true) && s5l8920_sdhc_attach(&board,&sdhc));
    CHECK(s5l8920_sdhc_write(&sdhc,8,4,0xdeadbeef));pending=sdhc;
    s5l8920_free(&board);CHECK(!board.sdhc && !memcmp(&pending,&sdhc,sizeof sdhc));
    init(true);wr(GATE,0x8000000f); /* No new raw reset coupling. */
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && board.clock_gate[S5L8920_SDHC_GATE].value==15);
    init(true);board.bus.write8(&board,REG(0x2f),1);pending=sdhc;
    iop.servicing=true;
    CHECK(!s5l8920_sdhc_attach(&board,&sdhc) && !s5l8920_sdhc_board_service(&board,true));
    iop.servicing=false;s5l8920_fmi_t fmi={.initialized=true,.servicing=true};board.fmi[0]=&fmi;
    CHECK(!s5l8920_sdhc_attach(&board,&sdhc) && !s5l8920_sdhc_board_service(&board,true));
    CHECK(!memcmp(&pending,&sdhc,sizeof sdhc));board.fmi[0]=NULL;
    CHECK(s5l8920_sdhc_board_service(&board,true) && !sdhc.reset_pending);
}
int main(void) {
    test_shared_state();test_iop_refusals();test_ap_refusals();test_lifetime();s5l8920_free(&board);
    printf("s5l8920_sdhc_board: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
