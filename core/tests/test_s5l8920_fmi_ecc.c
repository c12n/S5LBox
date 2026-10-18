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
static void board_init_modes(bool selected,bool pending) {
    s5l8920_free(&board);CHECK(s5l8920_init(&board));cold(selected?8:0);
    for(unsigned b=0;b<2;b++) {
        if(pending)CHECK(s5l8920_fmi_ecc_pending_configure(&f[b]));
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
static void board_init(bool selected) {board_init_modes(selected,false);}
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
static uint32_t pending_bits(unsigned n) {return ((n&1u)<<3)|((n&2u)<<4)|((n&4u)<<4);}
static void test_pending_knowledge(void) {
    cold(0);s5l8920_fmi_t empty={0};
    CHECK(!s5l8920_fmi_ecc_pending_configure(NULL) && !s5l8920_fmi_ecc_pending_configure(&empty));
    CHECK(!s5l8920_fmi_ecc_pending_supply(NULL,0));
    CHECK(s5l8920_fmi_write(&f[0],0x400,1));refused_write(&f[0],0x810,0x68);refused_read(&f[0],0x810);
    CHECK(!s5l8920_fmi_ecc_pending_configure(&f[0]) && !s5l8920_fmi_ecc_pending_supply(&f[0],0x68));
    cold(0);CHECK(s5l8920_fmi_write(&f[0],0x408,1));CHECK(!s5l8920_fmi_ecc_pending_configure(&f[0]));
    cold(0);CHECK(s5l8920_fmi_reset_line(&f[0],true));CHECK(!s5l8920_fmi_ecc_pending_configure(&f[0]));
    for(unsigned b=0;b<2;b++)for(unsigned n=0;n<8;n++) {
        cold(3);CHECK(s5l8920_fmi_ecc_pending_configure(&f[b]));
        refused_read(&f[b],0x810);refused_write(&f[b],0x810,0x68);
        CHECK(!s5l8920_fmi_ecc_pending_supply(&f[b],0x68));prepare(b);
        s5l8920_fmi_t before=f[b];
        CHECK(s5l8920_fmi_write(&f[b],0x810,0) && s5l8920_fmi_ecc_pending_supply(&f[b],0));
        CHECK(!memcmp(&before,&f[b],sizeof before));refused_read(&f[b],0x810);
        uint32_t bits=pending_bits(n),value=0xdeadbeef;
        CHECK(s5l8920_fmi_write(&f[b],0x810,bits));
        CHECK(f[b].ecc_pending_known==bits && !f[b].ecc_pending);
        if(bits!=0x68)refused_read(&f[b],0x810);
        CHECK(s5l8920_fmi_ecc_pending_supply(&f[b],0x68^bits));
        before=f[b];CHECK(s5l8920_fmi_ecc_pending_configure(&f[b]) && !memcmp(&before,&f[b],sizeof before));
        CHECK(s5l8920_fmi_read(&f[b],0x810,&value) && value==(0x68^bits));
        CHECK(s5l8920_fmi_read(&f[b],0x810,&value) && !memcmp(&before,&f[b],sizeof before));
        CHECK(!s5l8920_fmi_read(&f[b],0x810,NULL) && !memcmp(&before,&f[b],sizeof before));
    }
}
static void test_pending_events(void) {
    for(unsigned b=0;b<2;b++)for(unsigned initial=0;initial<8;initial++)
    for(unsigned ack=0;ack<8;ack++)for(unsigned events=0;events<8;events++) {
        cold(3);CHECK(s5l8920_fmi_ecc_pending_configure(&f[b]));prepare(b);
        CHECK(s5l8920_fmi_ecc_supply(&f[b],17,true));s5l8920_fmi_t peer=f[b^1u];
        CHECK(s5l8920_fmi_write(&f[b],0x810,0x68));
        CHECK(s5l8920_fmi_ecc_pending_supply(&f[b],pending_bits(initial)));
        s5l8920_fmi_t expected=f[b];expected.ecc_pending&=~pending_bits(ack);
        CHECK(s5l8920_fmi_write(&f[b],0x810,pending_bits(ack)));
        CHECK(!memcmp(&expected,&f[b],sizeof expected));
        CHECK(s5l8920_fmi_ecc_pending_supply(&f[b],pending_bits(events)));
        uint32_t value=0xdeadbeef,want=(pending_bits(initial)&~pending_bits(ack))|pending_bits(events);
        CHECK(s5l8920_fmi_read(&f[b],0x810,&value) && value==want);
        CHECK(!memcmp(&peer,&f[b^1u],sizeof peer));
        CHECK(s5l8920_fmi_write(&f[b],0x80c,1));
        CHECK(s5l8920_fmi_read(&f[b],0x810,&value) && value==want);
        CHECK(f[b].ecc_count==0 && !f[b].interrupt_pending && !f[b].status && !f[b].commands && !f[b].sectors_completed);
    }
}
static void test_pending_reset_and_refusal(void) {
    cold(3);CHECK(s5l8920_fmi_ecc_pending_configure(&f[0]));prepare(0);
    CHECK(s5l8920_fmi_ecc_pending_supply(&f[0],0x68));
    for(unsigned bit=0;bit<32;bit++)if(!((1u<<bit)&0x68)) {
        uint32_t invalid=(1u<<bit)|0x68;s5l8920_fmi_t before=f[0];
        refused_write(&f[0],0x810,invalid);
        CHECK(!s5l8920_fmi_ecc_pending_supply(&f[0],invalid) && !memcmp(&before,&f[0],sizeof before));
    }
    f[0].servicing=true;s5l8920_fmi_t active=f[0];
    refused_read(&f[0],0x810);refused_write(&f[0],0x810,0x68);
    CHECK(!s5l8920_fmi_ecc_pending_supply(&f[0],8) && !s5l8920_fmi_ecc_pending_configure(&f[0]));
    CHECK(!memcmp(&active,&f[0],sizeof active));f[0].servicing=false;
    CHECK(s5l8920_fmi_reset_line(&f[0],true));
    CHECK(f[0].ecc_pending_configured && !f[0].ecc_pending_known && !f[0].ecc_pending);
    active=f[0];CHECK(s5l8920_fmi_reset_line(&f[0],true) && !memcmp(&active,&f[0],sizeof active));
    refused_read(&f[0],0x810);refused_write(&f[0],0x810,0x68);
    CHECK(!s5l8920_fmi_ecc_pending_supply(&f[0],0x68));
    CHECK(s5l8920_fmi_reset_line(&f[0],false));CHECK(s5l8920_fmi_write(&f[0],0x400,1));
    refused_read(&f[0],0x810);CHECK(s5l8920_fmi_write(&f[0],0x810,0x68));
    uint32_t value=0xdeadbeef;CHECK(s5l8920_fmi_read(&f[0],0x810,&value) && !value);
    CHECK(s5l8920_fmi_init(&f[0]) && !f[0].ecc_pending_configured && !f[0].ecc_pending_known);
    for(unsigned busy=0;busy<4;busy++) {
        cold(3);CHECK(s5l8920_fmi_ecc_pending_configure(&f[0]));prepare(0);
        CHECK(s5l8920_fmi_ecc_supply(&f[0],3,true));
        if(busy==0) {CHECK(s5l8920_fmi_write(&f[0],0x414,0xff));CHECK(s5l8920_fmi_write(&f[0],0x410,1));}
        if(busy==1)f[0].sector_pending=true;
        if(busy==2)f[0].fifo_count[0]=1;
        if(busy==3)f[0].control=0x50;
        s5l8920_fmi_t expected=f[0];expected.ecc_pending_known=0x68;
        CHECK(s5l8920_fmi_write(&f[0],0x810,0x68));
        CHECK(!memcmp(&expected,&f[0],sizeof expected));
        CHECK(s5l8920_fmi_ecc_pending_supply(&f[0],0x20));expected.ecc_pending=0x20;
        CHECK(!memcmp(&expected,&f[0],sizeof expected));
    }
}
static void test_pending_board(void) {
    for(unsigned b=0;b<2;b++) {
        board_init_modes(true,true);uint32_t a=reg(b,0x810);
        wr(reg(b,0x80c),1);CHECK(s5l8920_fmi_ecc_supply(&f[b],6,true));
        instruction(0xe5801000,a,0x68);board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;arm_cpu_t ap=board.cpu;
        CHECK(step()==ARM_OK && f[b].ecc_pending_known==0x68 && !f[b].ecc_pending);
        CHECK(s5l8920_fmi_ecc_pending_supply(&f[b],0x28));CHECK(board.bus.read32(&board,a)==0x28);
        instruction(0xe5901000,a,0xdeadbeef);CHECK(step()==ARM_OK && iop.cpu.r[1]==0x28);
        wr(a,8);instruction(0xe5901000,a,0xdeadbeef);CHECK(step()==ARM_OK && iop.cpu.r[1]==0x20);
        instruction(0xe5801000,a,0x20);CHECK(step()==ARM_OK && board.bus.read32(&board,a)==0);
        CHECK(f[b].ecc_count==1 && !f[b].interrupt_pending && !f[b].status);
        CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason && !iop.bytes_written && !board.timebase_ticks);
    }
    for(unsigned b=0;b<2;b++)for(unsigned condition=0;condition<12;condition++) {
        board_init_modes(true,condition!=0);uint32_t a=reg(b,0x810),opcode=0xe5901000,value=0xdeadbeef;
        if(condition>1) {wr(a,0x68);CHECK(s5l8920_fmi_ecc_pending_supply(&f[b],8));}
        if(condition==2)board.fmi[b]=NULL;
        if(condition==3)wr(GATE(S5L8920_FMI_GATE+b),0);
        if(condition==4)wr(GATE(S5L8920_FMI_GATE+b),0x8000000f);
        if(condition==5)board.clock_gate[S5L8920_FMI_GATE+b].configured=false;
        if(condition==6)opcode=0xe5d01000;
        if(condition==7)opcode=0xe1d010b0;
        if(condition==8) {opcode=0xe5c01000;value=0x68;}
        if(condition==9) {opcode=0xe1c010b0;value=0x68;}
        if(condition==10) {opcode=0xe5801000;value=1;}
        if(condition==11)a-=8;
        instruction(opcode,a,value);arm_cpu_t ap=board.cpu;s5l8920_fmi_t before=f[b];
        CHECK(step()==ARM_HALT && iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED);
        CHECK(iop.failure.address==a && !memcmp(&before,&f[b],sizeof before));
        CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason);
    }
    for(unsigned write=0;write<2;write++)for(unsigned size=1;size<=4;size*=2)
    for(unsigned misalign=0;misalign<2;misalign++) {
        if(size==4 && !misalign)continue;
        board_init_modes(false,true);uint32_t a=reg(0,0x810);wr(a,0x68);
        CHECK(s5l8920_fmi_ecc_pending_supply(&f[0],0x68));s5l8920_fmi_t before=f[0];a+=misalign;
        if(write) {
            if(size==1)board.bus.write8(&board,a,0x68);
            else if(size==2)board.bus.write16(&board,a,0x68);
            else board.bus.write32(&board,a,0x68);
        } else {
            if(size==1)(void)board.bus.read8(&board,a);
            else if(size==2)(void)board.bus.read16(&board,a);
            else (void)board.bus.read32(&board,a);
        }
        CHECK(board.bus_failure.reason==S5L8920_BUS_ACCESS_UNIMPLEMENTED && !memcmp(&before,&f[0],sizeof before));
        s5l8920_iop_t stopped=iop;s5l8920_bus_failure_t fault=board.bus_failure;
        CHECK(step()==ARM_HALT && !memcmp(&stopped,&iop,sizeof stopped) && !memcmp(&fault,&board.bus_failure,sizeof fault));
    }
    for(unsigned write=0;write<2;write++) {
        board_init_modes(false,true);uint32_t a=reg(1,0x810);wr(a,0x68);
        CHECK(s5l8920_fmi_ecc_pending_supply(&f[1],0x68));s5l8920_fmi_t before=f[1];
        f[0].servicing=true;if(write)wr(a,0x68);else (void)board.bus.read32(&board,a);
        CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&before,&f[1],sizeof before));
        f[0].servicing=false;
    }
    s5l8920_clear_bus_failure(&board);CHECK(s5l8920_reset(&board) && !board.fmi[0] && !board.fmi[1]);
    CHECK(f[1].ecc_pending_configured && f[1].ecc_pending==0x68);s5l8920_free(&board);
}
static void test_page_programming_board(void) {
    const uint32_t offsets[]={0,0x808},values[]={0x64a29,0xf00};
    for(unsigned b=0;b<2;b++) {
        board_init_modes(true,true);wr(reg(b,0x80c),1);wr(reg(b,0x810),0x68);
        CHECK(s5l8920_fmi_ecc_supply(&f[b],7,true) && s5l8920_fmi_ecc_pending_supply(&f[b],8));
        s5l8920_fmi_t peer=f[b^1u];uint32_t backing[2][10];memcpy(backing,storage,sizeof backing);
        for(unsigned n=0;n<2;n++) {
            instruction(0xe5801000,reg(b,offsets[n]),values[n]);
            board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;arm_cpu_t ap=board.cpu;
            s5l8920_fmi_t expected=f[b];
            if(n) {expected.ecc_control=0xf00;expected.programmed|=2048u;}
            else {expected.sector_configuration=0x64a29;expected.programmed|=1024u;}
            CHECK(step()==ARM_OK && !board.bus_failure.reason);
            CHECK(!memcmp(&expected,&f[b],sizeof expected) && !memcmp(&peer,&f[b^1u],sizeof peer));
            CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.timebase_ticks && !iop.bytes_written);
            wr(reg(b,offsets[n]),values[n]);CHECK(!board.bus_failure.reason && !memcmp(&expected,&f[b],sizeof expected));
        }
        CHECK(!memcmp(backing,storage,sizeof backing));
        CHECK(f[b].ecc_count==1 && f[b].ecc_pending==8 && f[b].ecc_pending_known==0x68);
        wr(GATE(S5L8920_FMI_GATE+b),0x8000000f);
        CHECK(!board.bus_failure.reason && !f[b].sector_configuration && !f[b].ecc_control && !f[b].programmed);
    }
    for(unsigned b=0;b<2;b++)for(unsigned n=0;n<2;n++)for(unsigned condition=0;condition<5;condition++) {
        board_init(false);wr(reg(b,0),0x64a29);
        if(condition==0)wr(GATE(S5L8920_FMI_GATE+b),0);
        if(condition==1)wr(GATE(S5L8920_FMI_GATE+b),0x8000000f);
        if(condition==2)f[b^1u].servicing=true;
        if(condition==3)f[b].pending=1;
        if(condition==4)f[b].fifo_count[1]=1;
        instruction(0xe5801000,reg(b,offsets[n]),values[n]);
        s5l8920_fmi_t before=f[b];arm_cpu_t ap=board.cpu;
        CHECK(step()==ARM_HALT && !memcmp(&before,&f[b],sizeof before));
        CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason);
        wr(reg(b,offsets[n]),values[n]);
        CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&before,&f[b],sizeof before));
        f[b^1u].servicing=false;
    }
    for(unsigned n=0;n<2;n++)for(unsigned size=1;size<=4;size*=2) {
        board_init(false);wr(reg(0,0),0x64a29);s5l8920_fmi_t before=f[0];
        uint32_t a=reg(0,offsets[n]);
        if(size==1)board.bus.write8(&board,a,(uint8_t)values[n]);
        else if(size==2)board.bus.write16(&board,a,(uint16_t)values[n]);
        else wr(a+1,values[n]);
        CHECK(board.bus_failure.reason==S5L8920_BUS_ACCESS_UNIMPLEMENTED && !memcmp(&before,&f[0],sizeof before));
    }
    s5l8920_free(&board);
}
int main(void) {
    test_configuration();test_fifo();test_refusals_and_reset();test_board_shared();test_board_refusals();
    test_pending_knowledge();test_pending_events();test_pending_reset_and_refusal();test_pending_board();
    test_page_programming_board();
    printf("s5l8920_fmi_ecc: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
