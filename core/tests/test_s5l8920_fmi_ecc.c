/* Explicit ECC result storage, consumption, clear/reset and AP/IOP isolation.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
static unsigned passed,failed;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);}}while(0)
static s5l8920_fmi_t f[2];
static uint32_t storage[2][10];
static s5l8920_t board;
static s5l8920_iop_t iop;
#define CODE UINT32_C(0x41000000)
#define GATE(n) (S5L8920_CLOCK_GATE_BASE+4u*(n))
static uint32_t reg(unsigned bank,uint32_t offset) {return S5L8920_FMI_BASE+bank*S5L8920_FMI_STRIDE+offset;}
static void cold(unsigned depth) {
    for(unsigned b=0;b<2;b++) {
        f[b]=(s5l8920_fmi_t){0};CHECK(s5l8920_fmi_init(&f[b]));
        for(unsigned n=0;n<10;n++)storage[b][n]=0xa5a5a5a5;
        if(depth)CHECK(s5l8920_fmi_ecc_configure(&f[b],storage[b]+1,depth));
    }
}
static void prepare(unsigned b) {
    CHECK(s5l8920_fmi_write(&f[b],0x400,1));CHECK(s5l8920_fmi_write(&f[b],0x80c,1));
}
static void refused_read(s5l8920_fmi_t *v,uint32_t offset) {
    s5l8920_fmi_t before=*v;uint32_t value=0xdeadbeef;
    CHECK(!s5l8920_fmi_read(v,offset,&value) && value==0xdeadbeef);
    CHECK(!memcmp(&before,v,sizeof before));
}
static void refused_write(s5l8920_fmi_t *v,uint32_t offset,uint32_t value) {
    s5l8920_fmi_t before=*v;uint32_t backing[2][10];memcpy(backing,storage,sizeof backing);
    CHECK(!s5l8920_fmi_write(v,offset,value));
    CHECK(!memcmp(&before,v,sizeof before) && !memcmp(backing,storage,sizeof backing));
}
static void test_configuration(void) {
    cold(0);s5l8920_fmi_t empty={0};
    CHECK(!s5l8920_fmi_ecc_configure(NULL,storage[0],1));
    CHECK(!s5l8920_fmi_ecc_configure(&empty,storage[0],1));
    CHECK(!s5l8920_fmi_ecc_supply(NULL,0,false));
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],NULL,1));
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],storage[0],0));
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],storage[0],SIZE_MAX/sizeof(uint32_t)+1));
    CHECK(s5l8920_fmi_write(&f[0],0x400,1));refused_write(&f[0],0x80c,1);refused_read(&f[0],0x80c);
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],storage[0],1));
    CHECK(!s5l8920_fmi_ecc_supply(&f[0],0,false));
    cold(3);s5l8920_fmi_t before=f[0];
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],storage[1]+1,3));
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],storage[0]+1,2));
    CHECK(s5l8920_fmi_ecc_configure(&f[0],storage[0]+1,3) && !memcmp(&before,&f[0],sizeof before));
    refused_read(&f[0],0x80c);refused_write(&f[0],0x80c,1);CHECK(!s5l8920_fmi_ecc_supply(&f[0],0,false));
    CHECK(s5l8920_fmi_write(&f[0],0x400,1));refused_read(&f[0],0x80c);
    CHECK(!s5l8920_fmi_ecc_supply(&f[0],0,false));prepare(0);
    CHECK(s5l8920_fmi_ecc_supply(&f[0],5,true));before=f[0];
    CHECK(s5l8920_fmi_ecc_configure(&f[0],storage[0]+1,3) && !memcmp(&before,&f[0],sizeof before));
    uint32_t word=0;CHECK(s5l8920_fmi_read(&f[0],0x80c,&word) && word==0x50001);
    cold(0);CHECK(s5l8920_fmi_write(&f[0],0x408,1));
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],storage[0],1));
}
static void test_fifo(void) {
    const unsigned depths[]={1,3,8};
    for(unsigned d=0;d<3;d++)for(unsigned b=0;b<2;b++) {
        unsigned depth=depths[d];cold(depth);prepare(b);
        s5l8920_fmi_t peer=f[b^1u];refused_read(&f[b],0x80c);
        unsigned next=0;
        for(unsigned cycle=0;cycle<64;cycle++) {
            while(f[b].ecc_count<depth) {
                unsigned bits=next%32,flag=(next/32)&1u;
                CHECK(s5l8920_fmi_ecc_supply(&f[b],bits,flag!=0));next++;
            }
            s5l8920_fmi_t full=f[b];uint32_t bytes[10];memcpy(bytes,storage[b],sizeof bytes);
            CHECK(!s5l8920_fmi_ecc_supply(&f[b],0,false));
            CHECK(!memcmp(&full,&f[b],sizeof full) && !memcmp(bytes,storage[b],sizeof bytes));
            unsigned oldest=next-depth;uint32_t value=0xdeadbeef;
            CHECK(!s5l8920_fmi_read(&f[b],0x80c,NULL) && !memcmp(&full,&f[b],sizeof full));
            CHECK(s5l8920_fmi_read(&f[b],0x80c,&value));
            CHECK(value==((oldest%32)<<16|((oldest/32)&1u)) && f[b].ecc_count==depth-1);
            CHECK(!memcmp(&peer,&f[b^1u],sizeof peer));
            CHECK(storage[b][0]==0xa5a5a5a5 && storage[b][depth+1]==0xa5a5a5a5);
        }
        CHECK(s5l8920_fmi_write(&f[b],0x80c,1) && !f[b].ecc_count && !f[b].ecc_head);
        refused_read(&f[b],0x80c);CHECK(s5l8920_fmi_ecc_supply(&f[b],31,true));
        uint32_t value=0;CHECK(s5l8920_fmi_read(&f[b],0x80c,&value) && value==0x1f0001);
        refused_read(&f[b],0x80c);
    }
}
static void test_refusals_and_reset(void) {
    cold(8);prepare(0);CHECK(s5l8920_fmi_ecc_supply(&f[0],7,false));
    for(unsigned n=0;n<32;n++)if(n!=1)refused_write(&f[0],0x80c,n);
    refused_write(&f[0],0x80c,UINT32_MAX);
    for(unsigned offset=0x800;offset<0x820;offset+=4)if(offset!=0x80c) {
        refused_read(&f[0],offset);refused_write(&f[0],offset,1);
    }
    s5l8920_fmi_t before=f[0];CHECK(!s5l8920_fmi_ecc_supply(&f[0],32,false));
    CHECK(!memcmp(&before,&f[0],sizeof before));
    for(unsigned state=0;state<4;state++) {
        cold(8);prepare(0);CHECK(s5l8920_fmi_ecc_supply(&f[0],7,false));
        if(state==0) {CHECK(s5l8920_fmi_write(&f[0],0x414,0xff));CHECK(s5l8920_fmi_write(&f[0],0x410,1));}
        if(state==1)f[0].sector_pending=true;
        if(state==2)f[0].fifo_count[0]=1;
        if(state==3)f[0].control=0x50;
        refused_write(&f[0],0x80c,1);
    }
    cold(8);prepare(0);CHECK(s5l8920_fmi_ecc_supply(&f[0],7,true));before=f[0];
    f[0].servicing=true;s5l8920_fmi_t active=f[0];
    refused_read(&f[0],0x80c);refused_write(&f[0],0x80c,1);
    CHECK(!s5l8920_fmi_ecc_supply(&f[0],0,false));
    CHECK(!s5l8920_fmi_ecc_configure(&f[0],storage[0]+1,8));
    CHECK(!s5l8920_fmi_reset_line(&f[0],true) && !s5l8920_fmi_init(&f[0]));
    CHECK(!memcmp(&active,&f[0],sizeof active));f[0].servicing=false;
    CHECK(!memcmp(&before,&f[0],sizeof before));
    CHECK(s5l8920_fmi_reset_line(&f[0],true));
    CHECK(f[0].ecc_results==storage[0]+1 && f[0].ecc_capacity==8 && !f[0].ecc_clear_seen && !f[0].ecc_count);
    before=f[0];CHECK(s5l8920_fmi_reset_line(&f[0],true) && !memcmp(&before,&f[0],sizeof before));
    refused_read(&f[0],0x80c);refused_write(&f[0],0x80c,1);CHECK(!s5l8920_fmi_ecc_supply(&f[0],1,false));
    CHECK(s5l8920_fmi_reset_line(&f[0],false));CHECK(s5l8920_fmi_write(&f[0],0x400,1));
    refused_read(&f[0],0x80c);CHECK(!s5l8920_fmi_ecc_supply(&f[0],1,false));
    prepare(0);CHECK(s5l8920_fmi_ecc_supply(&f[0],9,false));
    uint32_t value=0;CHECK(s5l8920_fmi_read(&f[0],0x80c,&value) && value==0x90000);
    uint32_t bytes[10];memcpy(bytes,storage[0],sizeof bytes);
    CHECK(s5l8920_fmi_init(&f[0]) && !f[0].ecc_results && !f[0].ecc_capacity);
    CHECK(!memcmp(bytes,storage[0],sizeof bytes));
}
static void wr(uint32_t a,uint32_t v) {board.bus.write32(&board,a,v);}
static void board_init(bool selected) {
    s5l8920_free(&board);CHECK(s5l8920_init(&board));cold(selected?8:0);
    for(unsigned b=0;b<2;b++) {
        CHECK(s5l8920_clock_gate_configure(&board,S5L8920_FMI_GATE+b,15));
        CHECK(s5l8920_fmi_attach(&board,b,&f[b]));wr(reg(b,0x400),1);
    }
    s5l8920_iop_init(&iop);
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    CHECK(s5l8920_iop_attach(&board,&iop));wr(GATE(S5L8920_IOP_RESET_GATE),0x8000000f);wr(GATE(S5L8920_IOP_RESET_GATE),15);
    wr(S5L8920_IOP_BASE+0x110,CODE);wr(S5L8920_IOP_BASE+0x114,0x3ffff000);
    wr(S5L8920_IOP_BASE+0x118,S5L8920_IOP_BASE);wr(S5L8920_IOP_BASE+0x100,1);
    CHECK(!board.bus_failure.reason);
}
static void instruction(uint32_t opcode,uint32_t address,uint32_t value) {
    wr(CODE,opcode);iop.cpu.r[15]=0;iop.cpu.r[0]=address;iop.cpu.r[1]=value;
}
static arm_status_t step(void) {
    arm_status_t status=s5l8920_iop_board_service(&board);
    CHECK(!iop.servicing && !iop.memory && !iop.fmi && !iop.gate && !iop.memory_owner);
    return status;
}
static void test_board_shared(void) {
    for(unsigned b=0;b<2;b++) {
        board_init(true);uint32_t a=reg(b,0x80c);
        instruction(0xe5801000,a,1);board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;arm_cpu_t ap=board.cpu;
        CHECK(step()==ARM_OK && f[b].ecc_clear_seen && !f[b].ecc_count);
        CHECK(s5l8920_fmi_ecc_supply(&f[b],3,false) && s5l8920_fmi_ecc_supply(&f[b],15,true));
        instruction(0xe5901000,a,0xdeadbeef);CHECK(step()==ARM_OK && iop.cpu.r[1]==0x30000);
        CHECK(board.bus.read32(&board,a)==0xf0001 && !f[b].ecc_count);
        CHECK(s5l8920_fmi_ecc_supply(&f[b],9,false));wr(a,1);CHECK(!f[b].ecc_count);
        CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason && !iop.bytes_written && !board.timebase_ticks);
        instruction(0xe5901000,a,0xdeadbeef);arm_cpu_t before=iop.cpu;s5l8920_fmi_t peer=f[b];
        CHECK(step()==ARM_HALT && iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED);
        CHECK(!memcmp(before.r,iop.cpu.r,sizeof before.r) && before.cycles==iop.cpu.cycles && before.cpsr==iop.cpu.cpsr);
        CHECK(!memcmp(&peer,&f[b],sizeof peer) && !memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason);
    }
}
static void test_board_refusals(void) {
    for(unsigned b=0;b<2;b++)for(unsigned condition=0;condition<9;condition++) {
        board_init(condition!=0);uint32_t a=reg(b,0x80c),opcode=0xe5901000,value=0xdeadbeef;
        if(condition) {wr(a,1);CHECK(s5l8920_fmi_ecc_supply(&f[b],4,false));}
        if(condition==1)board.fmi[b]=NULL;
        if(condition==2)wr(GATE(S5L8920_FMI_GATE+b),0);
        if(condition==3)wr(GATE(S5L8920_FMI_GATE+b),0x8000000f);
        if(condition==4)board.clock_gate[S5L8920_FMI_GATE+b].configured=false;
        if(condition==5)opcode=0xe5d01000;
        if(condition==6) {opcode=0xe5c01000;value=1;}
        if(condition==7) {opcode=0xe5801000;value=2;}
        if(condition==8)a+=4;
        instruction(opcode,a,value);arm_cpu_t ap=board.cpu;s5l8920_fmi_t before=f[b];
        CHECK(step()==ARM_HALT && iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED);
        CHECK(iop.failure.address==a && !memcmp(&before,&f[b],sizeof before));
        CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason);
    }
    for(unsigned size=1;size<=4;size*=2)for(unsigned misalign=0;misalign<2;misalign++) {
        if(size==4 && !misalign)continue;
        board_init(true);uint32_t a=reg(0,0x80c);wr(a,1);CHECK(s5l8920_fmi_ecc_supply(&f[0],2,false));
        s5l8920_fmi_t before=f[0];a+=misalign;
        if(size==1)(void)board.bus.read8(&board,a);
        else if(size==2)(void)board.bus.read16(&board,a);
        else (void)board.bus.read32(&board,a);
        CHECK(board.bus_failure.reason==S5L8920_BUS_ACCESS_UNIMPLEMENTED && !memcmp(&before,&f[0],sizeof before));
        s5l8920_bus_failure_t fault=board.bus_failure;s5l8920_iop_t stopped=iop;
        CHECK(step()==ARM_HALT && !memcmp(&stopped,&iop,sizeof stopped) && !memcmp(&fault,&board.bus_failure,sizeof fault));
    }
    board_init(true);wr(reg(1,0x80c),1);CHECK(s5l8920_fmi_ecc_supply(&f[1],3,false));
    f[0].servicing=true;s5l8920_fmi_t before=f[1];
    (void)board.bus.read32(&board,reg(1,0x80c));
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&before,&f[1],sizeof before));
    f[0].servicing=false;s5l8920_clear_bus_failure(&board);
    CHECK(board.bus.read32(&board,reg(1,0x80c))==0x30000);
    CHECK(s5l8920_reset(&board) && !board.fmi[0] && !board.fmi[1]);
    CHECK(f[1].ecc_results==storage[1]+1);s5l8920_free(&board);
}
int main(void) {
    test_configuration();test_fifo();test_refusals_and_reset();test_board_shared();test_board_refusals();
    printf("s5l8920_fmi_ecc: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
