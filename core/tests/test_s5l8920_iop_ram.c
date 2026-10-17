/* Version1 RAM conversion, real CPU accesses, shared backing and cache aliases.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define FW 0x41000000u
#define HEAP 0x01718000u
#define PHYS(a) (S5L8920_RAM_BASE+(a))
#define CTRL (S5L8920_IOP_BASE+0x100u)
#define RESET (S5L8920_CLOCK_GATE_BASE+4*S5L8920_IOP_RESET_GATE)
static s5l8920_t board;
static s5l8920_iop_t iop;
static s5l8920_iop_cache_line_t lines[16];
static unsigned passed,failed;
static uint32_t deny,last_physical;
static bool try_reentry;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);}} while(0)
static void wr(uint32_t a,uint32_t v){board.bus.write32(&board,a,v);}
static uint32_t rd(uint32_t a){return board.bus.read32(&board,a);}
static void init(bool mapped,unsigned capacity){
    s5l8920_free(&board);CHECK(s5l8920_init(&board));s5l8920_iop_init(&iop);
    deny=UINT32_MAX;last_physical=0;try_reentry=false;
    if(mapped)CHECK(s5l8920_iop_ram_configure(&iop,S5L8920_RAM_SIZE));
    if(capacity)CHECK(s5l8920_iop_cache_configure(&iop,lines,capacity));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    CHECK(s5l8920_iop_attach(&board,&iop));wr(RESET,0x8000000fu);wr(RESET,15);
    wr(FW,0xe5801000u);wr(FW+4,0xe5901000u); /* STR/LDR r1,[r0]. */
    wr(FW+8,0xe5c01000u);wr(FW+12,0xe5d01000u); /* STRB/LDRB. */
    wr(FW+16,0xe8a00006u); /* STMIA r0!,{r1,r2}. */
    wr(FW+20,0xe1c010b0u);wr(FW+24,0xe1d010b0u); /* STRH/LDRH r1,[r0]. */
    wr(FW+28,0xe1d010d0u);wr(FW+32,0xe1d010f0u); /* LDRSB/LDRSH r1,[r0]. */
    wr(FW+36,0xe18520b0u); /* Original IOP STRH r2,[r5,r0]. */
    wr(S5L8920_IOP_BASE+0x118,S5L8920_IOP_BASE);wr(S5L8920_IOP_BASE+0x110,FW);
    wr(S5L8920_IOP_BASE+0x114,0x3ffff000u);wr(CTRL,1);
    CHECK(!board.bus_failure.reason && iop.running);
}
static arm_status_t execute(unsigned pc,uint32_t a,uint32_t v){
    iop.cpu.r[15]=pc;iop.cpu.r[0]=a;iop.cpu.r[1]=v;
    return s5l8920_iop_board_service(&board);
}
static void store(uint32_t a,uint32_t v){CHECK(execute(0,a,v)==ARM_OK);}
static uint32_t load(uint32_t a){CHECK(execute(4,a,0)==ARM_OK);return iop.cpu.r[1];}
static void command(unsigned offset,uint32_t value){store(S5L8920_IOP_BASE+offset,value);}
static void enable(void){command(0x20,1);command(0,4);}
static bool stopped_cpu(const arm_cpu_t *before){
    arm_cpu_t after=iop.cpu;
    CHECK(!after.abort_pending && after.abort_fsr==ARM_MMU_BUS_FAILURE && after.abort_far==iop.failure.address);
    after.abort_pending=before->abort_pending;after.abort_fsr=before->abort_fsr;after.abort_far=before->abort_far;
    return !memcmp(before,&after,sizeof after);
}
static uint8_t *provider(void *owner,uint32_t a,uint32_t size){
    CHECK(owner==&board);last_physical=a;
    if(try_reentry){
        s5l8920_iop_t before=iop;
        CHECK(!s5l8920_iop_ram_configure(&iop,S5L8920_RAM_SIZE));
        CHECK(!memcmp(&before,&iop,sizeof before));
    }
    if(a==deny || a<S5L8920_RAM_BASE || size>S5L8920_RAM_SIZE || a-S5L8920_RAM_BASE>S5L8920_RAM_SIZE-size)return NULL;
    return board.ram+(a-S5L8920_RAM_BASE);
}
static void test_selection(void){
    s5l8920_iop_t blank={0};CHECK(!s5l8920_iop_ram_configure(NULL,4096));
    CHECK(!s5l8920_iop_ram_configure(&blank,4096));s5l8920_iop_init(&iop);
    const uint32_t bad[]={0,1,4095,4097,0x40001000u,UINT32_MAX};
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];n++)CHECK(!s5l8920_iop_ram_configure(&iop,bad[n]) && !iop.ram_size);
    CHECK(s5l8920_iop_ram_configure(&iop,0x40000000u));
    CHECK(s5l8920_iop_ram_configure(&iop,0x40000000u));
    CHECK(!s5l8920_iop_ram_configure(&iop,4096) && iop.ram_size==0x40000000u);
    s5l8920_iop_init(&iop);CHECK(!iop.ram_size);
    init(false,0);CHECK(!s5l8920_iop_ram_configure(&iop,S5L8920_RAM_SIZE));
    CHECK(execute(0,HEAP,0x1234)==ARM_HALT && iop.failure.reason==S5L8920_IOP_UNMAPPED);
    CHECK(!iop.bytes_written && !board.bus_failure.reason && !iop.ram_size);
}
static void test_shared_mapping(void){
    init(true,0);wr(PHYS(HEAP),0x11223344);wr(FW+HEAP,0x55667788);
    CHECK(load(HEAP)==0x11223344);arm_cpu_t ap=board.cpu;
    store(HEAP,0xaabbccdd);CHECK(rd(PHYS(HEAP))==0xaabbccdd && rd(FW+HEAP)==0x55667788);
    CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && iop.bytes_written==4);
    board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;store(HEAP+4,7);
    CHECK(!board.cpu.excl_valid && !board.cpu.a8_excl_size && iop.bytes_written==8);
    wr(PHYS(0x80),0xaaaaaaaa);wr(FW+0x80,0x12345678);
    CHECK(load(0x80)==0x12345678 && load(FW-S5L8920_RAM_BASE+0x80)==0x12345678);
    store(FW-S5L8920_RAM_BASE+0x80,0x87654321);
    CHECK(load(0x80)==0x87654321 && rd(PHYS(0x80))==0xaaaaaaaa);
    wr(FW+0xffc,0x88776655);wr(PHYS(0x1000),0x44332211);wr(FW+0x1000,0xdeadbeef);
    CHECK(load(0xffc)==0x88776655 && load(0x1000)==0x44332211);
    CHECK(execute(8,S5L8920_RAM_SIZE-1,0x6b)==ARM_OK);
    CHECK(execute(12,S5L8920_RAM_SIZE-1,0)==ARM_OK && iop.cpu.r[1]==0x6b);
    wr(PHYS(HEAP),0xe3a0302au);iop.cpu.r[15]=HEAP;
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.r[3]==42 && iop.cpu.r[15]==HEAP+4);
    CHECK(!iop.memory && !iop.memory_owner && !iop.servicing);
    wr(CTRL,0);s5l8920_iop_t held=iop;
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && !memcmp(&held,&iop,sizeof iop));
    wr(RESET,0x8000000fu);wr(RESET,15);
    CHECK(iop.ram_size==S5L8920_RAM_SIZE && !iop.programmed && !iop.running && rd(PHYS(HEAP))==0xe3a0302au);
    CHECK(s5l8920_iop_ram_configure(&iop,S5L8920_RAM_SIZE));
}
static void test_failures(void){
    const uint32_t bad[]={S5L8920_RAM_SIZE,0x3ffffffcu,0x40000000u,0x41718000u,0x84304000u,0xfffffffcu};
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];n++){
        init(true,0);iop.cpu.r[0]=bad[n];iop.cpu.r[1]=0xdeadbeef;arm_cpu_t before=iop.cpu;
        CHECK(s5l8920_iop_board_service(&board)==ARM_HALT && stopped_cpu(&before));
        CHECK(iop.failure.reason==S5L8920_IOP_UNMAPPED && iop.failure.address==bad[n] &&
              iop.failure.write && iop.failure.size==4 && iop.failure.value==0xdeadbeef && !iop.bytes_written);
        CHECK(!board.bus_failure.reason && iop.running && rd(CTRL)==1);
    }
    init(true,0);deny=PHYS(HEAP);iop.cpu.r[0]=HEAP;iop.cpu.r[1]=9;arm_cpu_t before=iop.cpu;
    CHECK(s5l8920_iop_service(&iop,provider,&board)==ARM_HALT && stopped_cpu(&before));
    CHECK(iop.failure.reason==S5L8920_IOP_MEMORY_UNAVAILABLE && last_physical==PHYS(HEAP));
    CHECK(iop.failure.address==HEAP && !iop.bytes_written && !iop.memory_owner && !iop.memory);
    init(true,0);iop.cpu.r[15]=16;iop.cpu.r[0]=S5L8920_RAM_SIZE-4;
    iop.cpu.r[1]=0x12345678;iop.cpu.r[2]=0xabcdef01;before=iop.cpu;
    board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT && stopped_cpu(&before));
    CHECK(iop.failure.address==S5L8920_RAM_SIZE && iop.bytes_written==4 && rd(PHYS(S5L8920_RAM_SIZE-4))==0x12345678);
    CHECK(!board.cpu.excl_valid && !board.cpu.a8_excl_size);
    init(true,0);try_reentry=true;iop.cpu.r[0]=HEAP;iop.cpu.r[1]=7;
    CHECK(s5l8920_iop_service(&iop,provider,&board)==ARM_OK && rd(PHYS(HEAP))==7);
}
static void test_cache_and_aliases(void){
    init(true,16);wr(PHYS(HEAP),0x12345678);enable();CHECK(load(HEAP)==0x12345678);
    wr(PHYS(HEAP),0x11223344);CHECK(load(HEAP)==0x12345678);
    command(0x18,HEAP);command(0x24,0);CHECK(load(HEAP)==0x11223344);
    board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;store(HEAP,0x87654321);
    CHECK(rd(PHYS(HEAP))==0x11223344 && !iop.bytes_written && board.cpu.excl_valid);
    command(0x1c,0);CHECK(rd(PHYS(HEAP))==0x87654321 && iop.bytes_written==16 && !board.cpu.excl_valid);
    uint32_t alias=FW-S5L8920_RAM_BASE+0x100;wr(FW+0x100,5);
    CHECK(load(0x100)==5 && load(alias)==5);store(0x100,6);CHECK(load(alias)==5);
    command(0x18,0x100);command(0x1c,0);CHECK(rd(FW+0x100)==6 && load(alias)==5);
    command(0x18,alias);command(0x24,0);CHECK(load(alias)==6);
    CHECK(execute(0,S5L8920_IOP_BASE+0x18,S5L8920_RAM_SIZE)==ARM_HALT && iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED);
    init(true,16);enable();store(HEAP,0x55);command(0x18,HEAP);deny=PHYS(HEAP);
    iop.cpu.r[15]=0;iop.cpu.r[0]=S5L8920_IOP_BASE+0x1c;iop.cpu.r[1]=0;
    CHECK(s5l8920_iop_service(&iop,provider,&board)==ARM_HALT);
    CHECK(iop.failure.reason==S5L8920_IOP_MEMORY_UNAVAILABLE && iop.failure.address==HEAP && iop.failure.size==16 && iop.failure.write);
    CHECK(!iop.bytes_written && rd(PHYS(HEAP))==0);
    init(true,2);enable();store(HEAP,0x55);
    CHECK(execute(0,HEAP+16,0x66)==ARM_HALT && iop.failure.reason==S5L8920_IOP_CACHE_LIMIT);
    CHECK(!iop.bytes_written && rd(PHYS(HEAP))==0);
}
static void test_halfword_backing(void){
    for(unsigned cached=0;cached<2;++cached){
        init(true,cached?16:0);uint32_t a=HEAP+0x1b8;
        wr(PHYS(a),0xa5a5a5a5u);if(cached)enable();
        board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
        iop.cpu.r[15]=36;iop.cpu.r[5]=HEAP;iop.cpu.r[0]=0x1b8;iop.cpu.r[2]=0xabcdffffu;
        CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.r[15]==40);
        CHECK(iop.cpu.r[5]==HEAP && iop.cpu.r[0]==0x1b8 && iop.cpu.r[2]==0xabcdffffu);
        CHECK(rd(PHYS(a))==(cached?0xa5a5a5a5u:0xa5a5ffffu));
        CHECK(iop.bytes_written==(cached?0u:2u) && board.cpu.excl_valid==(cached!=0));
        CHECK(execute(24,a,0)==ARM_OK && iop.cpu.r[1]==0xffff);
        CHECK(execute(32,a,0)==ARM_OK && iop.cpu.r[1]==0xffffffffu);
        CHECK(execute(28,a+1,0)==ARM_OK && iop.cpu.r[1]==0xffffffffu);
        CHECK(execute(20,a,0x12348080u)==ARM_OK);
        CHECK(execute(24,a,0)==ARM_OK && iop.cpu.r[1]==0x8080);
        CHECK(execute(32,a,0)==ARM_OK && iop.cpu.r[1]==0xffff8080u);
        CHECK(execute(28,a+1,0)==ARM_OK && iop.cpu.r[1]==0xffffff80u);
        if(cached){
            CHECK(rd(PHYS(a))==0xa5a5a5a5u && !iop.bytes_written && board.cpu.excl_valid);
            command(0x18,a&~15u);command(0x1c,0);
        }
        CHECK(rd(PHYS(a))==0xa5a58080u && iop.bytes_written==(cached?16u:4u));
        CHECK(!board.cpu.excl_valid && !board.cpu.a8_excl_size && !board.bus_failure.reason);
        CHECK(!iop.failure.reason && !iop.servicing && !iop.memory && !iop.memory_owner);
        /* An odd halfword access is refused by the CPU before any RAM/cache
         * operation. It must not turn into an aligned store or a device fault. */
        uint64_t retired=iop.cpu.cycles,written=iop.bytes_written;
        CHECK(execute(20,a+1,0x1234)==ARM_UNDEFINED && iop.cpu.r[15]==20);
        CHECK(!iop.failure.reason && !board.bus_failure.reason && iop.cpu.cycles==retired && iop.bytes_written==written);
        CHECK(rd(PHYS(a))==0xa5a58080u);
    }
}
int main(void){
    test_selection();test_shared_mapping();test_failures();test_cache_and_aliases();
    test_halfword_backing();
    s5l8920_free(&board);printf("s5l8920_iop_ram: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
