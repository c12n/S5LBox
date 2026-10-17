/* Explicit functional cache policy: visibility, actual maintenance and limits.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define RAM 0x41000000u
#define CTRL (S5L8920_IOP_BASE+0x100u)
#define RESET (S5L8920_CLOCK_GATE_BASE+4*S5L8920_IOP_RESET_GATE)
static s5l8920_t board;
static s5l8920_iop_t iop;
static s5l8920_iop_cache_line_t lines[32],other[32];
static unsigned passed,failed;
static uint32_t deny;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);}} while(0)
static void wr(uint32_t a,uint32_t v){board.bus.write32(&board,a,v);}
static uint32_t rd(uint32_t a){return board.bus.read32(&board,a);}
static void init(size_t capacity){
    s5l8920_free(&board);CHECK(s5l8920_init(&board));s5l8920_iop_init(&iop);
    memset(lines,0xa5,sizeof lines);deny=UINT32_MAX;
    if(capacity)CHECK(s5l8920_iop_cache_configure(&iop,lines,capacity));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    CHECK(s5l8920_iop_attach(&board,&iop));wr(RESET,0x8000000fu);wr(RESET,15);
    memset(board.ram+RAM-S5L8920_RAM_BASE,0,0x1000);
    wr(RAM,0xe5801000u);wr(RAM+4,0xe5901000u); /* STR/LDR r1,[r0]. */
    wr(RAM+8,0xe5c01000u);wr(RAM+12,0xe5d01000u); /* STRB/LDRB. */
    wr(RAM+16,0xe8a00006u); /* STMIA r0!,{r1,r2}. */
    wr(S5L8920_IOP_BASE+0x118,S5L8920_IOP_BASE);wr(S5L8920_IOP_BASE+0x110,RAM);
    wr(S5L8920_IOP_BASE+0x114,0x3ffff000u);wr(CTRL,1);
    CHECK(!board.bus_failure.reason && iop.running);
}
static arm_status_t execute(unsigned pc,uint32_t address,uint32_t value){
    iop.cpu.r[15]=pc;iop.cpu.r[0]=address;iop.cpu.r[1]=value;
    return s5l8920_iop_board_service(&board);
}
static void command(unsigned offset,uint32_t value){CHECK(execute(0,S5L8920_IOP_BASE+offset,value)==ARM_OK);}
static uint32_t load(uint32_t address){CHECK(execute(4,address,0xaabbccddu)==ARM_OK);return iop.cpu.r[1];}
static void store(uint32_t address,uint32_t value){CHECK(execute(0,address,value)==ARM_OK);}
static void enable(void){command(0x20,1);command(0,4);CHECK(iop.cache_ready && iop.cache_enabled);}
static void select_line(uint32_t address){command(0x18,address);}
static void test_visibility(void){
    init(32);wr(RAM+0x100,0x11223344);wr(RAM+0x104,0xabcdef01);enable();
    CHECK(load(0x100)==0x11223344);wr(RAM+0x100,0x55667788);
    CHECK(load(0x100)==0x11223344);select_line(0x100);command(0x24,0);
    CHECK(load(0x100)==0x55667788 && !iop.bytes_written);
    board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    store(0x100,0x99887766);CHECK(load(0x100)==0x99887766 && rd(RAM+0x100)==0x55667788);
    CHECK(board.cpu.excl_valid && board.cpu.a8_excl_size==4 && !iop.bytes_written);
    /* Signaling the AP does not publish private cached data. */
    store(S5L8920_VIC_BASE+PL192_SOFTINT,8);
    CHECK(board.vic[0].soft==8 && rd(RAM+0x100)==0x55667788 && !iop.bytes_written);
    command(0x1c,0);CHECK(rd(RAM+0x100)==0x99887766 && rd(RAM+0x104)==0xabcdef01);
    CHECK(iop.bytes_written==16 && !board.cpu.excl_valid && !board.cpu.a8_excl_size);
    wr(RAM+0x100,0x12345678);CHECK(load(0x100)==0x99887766); /* Clean retains valid data. */
    command(0x1c,0);CHECK(rd(RAM+0x100)==0x12345678 && iop.bytes_written==16);
    store(0x100,0xdeadbeef);board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    command(0x24,0);CHECK(load(0x100)==0x12345678 && iop.bytes_written==16);
    CHECK(board.cpu.excl_valid && board.cpu.a8_excl_size==4); /* Dirty discard is not writeback. */
    CHECK(execute(8,0x103,0x7f)==ARM_OK);CHECK(execute(12,0x103,0)==ARM_OK && iop.cpu.r[1]==0x7f);
    command(0x1c,0);CHECK(rd(RAM+0x100)==0x7f345678 && iop.bytes_written==32);
}
static void test_whole_and_disable(void){
    init(32);enable();store(0x100,1);store(0x200,2);command(0,0);
    CHECK(load(0x100)==0 && load(0x200)==0);store(0x104,3);CHECK(rd(RAM+0x104)==3);
    command(0x0c,0);CHECK(rd(RAM+0x100)==1 && rd(RAM+0x200)==2 && rd(RAM+0x104)==0);
    CHECK(iop.bytes_written==36); /* One uncached word plus two real line writes. */
    wr(RAM+0x100,4);command(0,4);CHECK(load(0x100)==1);
    command(0,0);command(0x20,0);command(0,4);CHECK(load(0x100)==4);
    store(0x200,5);command(0,0);command(0x20,0);command(0,4);
    CHECK(load(0x200)==2 && iop.bytes_written==36);
    /* A real multiple transfer crosses the maintenance granule boundary. */
    iop.cpu.r[2]=0x22222222;CHECK(execute(16,0x10c,0x11111111)==ARM_OK);
    CHECK(iop.cpu.r[0]==0x114 && rd(RAM+0x10c)==0 && rd(RAM+0x110)==0);
    select_line(0x100);command(0x1c,0);CHECK(rd(RAM+0x10c)==0x11111111 && rd(RAM+0x110)==0);
    select_line(0x110);command(0x1c,0);CHECK(rd(RAM+0x110)==0x22222222 && iop.bytes_written==68);
}
static uint8_t *memory(void *owner,uint32_t address,uint32_t size){
    s5l8920_t *m=owner;
    if(address<RAM || size>0x1000 || address-RAM>0x1000-size ||
       (deny!=UINT32_MAX && address<=RAM+deny && address+size>RAM+deny))return NULL;
    return m->ram+address-S5L8920_RAM_BASE;
}
static void test_failures(void){
    init(2);enable();CHECK(load(0x100)==0); /* Code and data consume the two resident entries. */
    CHECK(execute(4,0x200,0x12345678)==ARM_HALT);
    CHECK(iop.failure.reason==S5L8920_IOP_CACHE_LIMIT && iop.failure.address==0x200);
    CHECK(iop.cpu.r[15]==4 && iop.cpu.r[1]==0x12345678 && iop.running && !board.bus_failure.reason);
    CHECK(!iop.memory && !iop.memory_owner && !iop.signal_ap && !iop.bytes_written);
    init(32);enable();store(0x100,0x11111111);store(0x200,0x22222222);deny=0x200;
    iop.cpu.r[15]=0;iop.cpu.r[0]=S5L8920_IOP_BASE+0x0c;iop.cpu.r[1]=0;
    uint64_t cycles=iop.cpu.cycles;
    CHECK(s5l8920_iop_service(&iop,memory,&board)==ARM_HALT);
    CHECK(iop.failure.reason==S5L8920_IOP_MEMORY_UNAVAILABLE && iop.failure.address==0x200 && iop.failure.size==16 && iop.failure.write);
    CHECK(rd(RAM+0x100)==0x11111111 && rd(RAM+0x200)==0 && iop.bytes_written==16);
    CHECK(iop.cpu.r[15]==0 && iop.cpu.cycles==cycles && iop.running && !board.bus_failure.reason);
    CHECK(!iop.memory && !iop.memory_owner && !iop.signal_ap && !iop.servicing);
    bool first_clean=false,second_dirty=false;
    for(unsigned n=0;n<32;n++)if(lines[n].valid){
        if(lines[n].address==0x100)first_clean=!lines[n].dirty;
        if(lines[n].address==0x200)second_dirty=lines[n].dirty;
    }
    CHECK(first_clean && second_dirty);
    init(32);enable();CHECK(load(0x100)==0);deny=0x200;
    iop.cpu.r[15]=4;iop.cpu.r[0]=0x200;iop.cpu.r[1]=0xfeedface;
    CHECK(s5l8920_iop_service(&iop,memory,&board)==ARM_HALT);
    CHECK(iop.failure.reason==S5L8920_IOP_MEMORY_UNAVAILABLE && !iop.failure.write && iop.cpu.r[1]==0xfeedface);
}
static void test_boundaries(void){
    init(0);CHECK(!s5l8920_iop_cache_configure(&iop,lines,32));
    CHECK(execute(0,S5L8920_IOP_BASE+0x20,1)==ARM_HALT && iop.failure.reason==S5L8920_IOP_UNMAPPED);
    s5l8920_iop_init(&iop);
    CHECK(!s5l8920_iop_cache_configure(NULL,lines,32));
    CHECK(!s5l8920_iop_cache_configure(&iop,NULL,32));
    CHECK(!s5l8920_iop_cache_configure(&iop,lines,0));
    CHECK(!s5l8920_iop_cache_configure(&iop,lines,SIZE_MAX));
    CHECK(s5l8920_iop_cache_configure(&iop,lines,32));
    CHECK(!s5l8920_iop_cache_configure(&iop,other,32));
    init(32);enable();store(0x100,123);s5l8920_iop_t before=iop;
    memcpy(other,lines,sizeof lines);
    CHECK(s5l8920_iop_cache_configure(&iop,lines,32));
    CHECK(!memcmp(&before,&iop,sizeof iop) && !memcmp(lines,other,sizeof lines));
    CHECK(!s5l8920_iop_reset_line(&iop,true));
    CHECK(!memcmp(&before,&iop,sizeof iop) && !memcmp(lines,other,sizeof lines));
    wr(CTRL,0);CHECK(!iop.running);wr(CTRL,1);CHECK(iop.running && load(0x100)==123 && rd(RAM+0x100)==0);
    wr(CTRL,0);CHECK(!s5l8920_iop_write(&iop,0x110,RAM+0x1000,memory,&board));
    CHECK(iop.firmware_base==RAM && !memcmp(lines,other,sizeof lines));
    s5l8920_iop_init(&iop);CHECK(!iop.cache_lines && !iop.cache_ready && !iop.cache_enabled);
    CHECK(!memcmp(lines,other,sizeof lines));
    const struct {unsigned pc,offset;uint32_t value;bool ready;} bad[]={
        {0,0,4,false},{0,0x20,0,false},{0,0x18,0x100,false},
        {0,0,1,true},{0,0x20,1,true},{0,0x08,1,true},{0,0x10,0,true},
        {0,0x18,0x101,true},{0,0x18,0x1000,true},{0,0x1c,0,true},
        {0,0x24,0,true},{0,0x0c,1,true},{4,0,0,true},{8,0x20,0,true}
    };
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];n++){
        init(32);if(bad[n].ready)enable();
        memcpy(other,lines,sizeof lines);
        CHECK(execute(bad[n].pc,S5L8920_IOP_BASE+bad[n].offset,bad[n].value)==ARM_HALT);
        CHECK(iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED && iop.running && !board.bus_failure.reason);
        CHECK(!iop.bytes_written && !iop.memory && !iop.signal_ap);
        /* A failed command may have fetched its own code into a valid line,
         * but it must not dirty data or alter the initial tag state. */
        if(!bad[n].ready)CHECK(!memcmp(lines,other,sizeof lines));
        else for(unsigned k=0;k<32;k++)CHECK(!lines[k].dirty);
    }
}
int main(void){
    test_visibility();test_whole_and_disable();test_failures();test_boundaries();
    s5l8920_free(&board);printf("s5l8920_iop_cache: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
