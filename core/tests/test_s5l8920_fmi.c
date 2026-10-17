/* Polled command/data effects, capability refusal and board lifetime tests.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
static unsigned passed,failed;
#define CHECK(v) do { if(v)passed++;else { failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#v); } } while(0)
static s5l8920_t board;
static s5l8920_fmi_t fmi,other;
typedef struct {
    s5l8920_fmi_t *controller;
    s5l8920_t *board;
    unsigned calls,resets,last_chip;
    uint8_t last_address;
    bool refuse,reenter,dirty,idle_bus;
    uint8_t ids[8][8];
} endpoint_t;
static endpoint_t endpoint;
static bool device(void *owner,unsigned chip,uint8_t command,uint8_t address,uint8_t id[8]) {
    endpoint_t *e=owner;CHECK(e && chip<=S5L8920_FMI_NO_CHIP);if(!e || chip>S5L8920_FMI_NO_CHIP)return false;
    e->calls++;
    if(e->reenter) {
        s5l8920_fmi_t before=*e->controller;uint32_t value=0xa5a5a5a5;
        CHECK(!s5l8920_fmi_init(e->controller));
        CHECK(!s5l8920_fmi_reset_line(e->controller,true));
        CHECK(!s5l8920_fmi_write(e->controller,0x400,1));
        CHECK(!s5l8920_fmi_write(e->controller,0x408,0xffff));
        CHECK(!s5l8920_fmi_read(e->controller,0x444,&value) && value==0xa5a5a5a5);
        CHECK(!s5l8920_fmi_service(e->controller,device,e));
        CHECK(!memcmp(&before,e->controller,sizeof before));
        if(e->board) {
            uint8_t *ram=e->board->ram;
            CHECK(!s5l8920_reset(e->board));s5l8920_free(e->board);CHECK(e->board->ram==ram);
            CHECK(!s5l8920_fmi_attach(e->board,0,e->controller));
            CHECK(!s5l8920_fmi_board_service(e->board,0,device,e));
        }
    }
    if(e->refuse || (chip==S5L8920_FMI_NO_CHIP && !e->idle_bus)) { memset(id,0x77,8);return false; }
    e->last_chip=chip;e->last_address=address;
    if(command==0xff) { if(chip<8) { e->resets++;e->dirty=false; } return true; }
    CHECK(command==0x90);if(command!=0x90)return false;
    if(chip==S5L8920_FMI_NO_CHIP)memset(id,0xff,8); /* Explicit test bus samples. */
    else for(unsigned n=0;n<8;n++)id[n]=(uint8_t)(e->ids[chip][n]^address);
    return true;
}
static void init(void) {
    s5l8920_free(&board);CHECK(s5l8920_fmi_init(&fmi));CHECK(s5l8920_fmi_init(&other));
    memset(&endpoint,0,sizeof endpoint);endpoint.controller=&fmi;
    for(unsigned c=0;c<8;c++)for(unsigned n=0;n<8;n++)endpoint.ids[c][n]=(uint8_t)(c*16+n+1);
}
static void wr(uint32_t o,uint32_t v) { CHECK(s5l8920_fmi_write(&fmi,o,v)); }
static uint32_t rd(uint32_t o) { uint32_t v=0xdeadbeef;CHECK(s5l8920_fmi_read(&fmi,o,&v));return v; }
static void reject(uint32_t o,uint32_t v) {
    s5l8920_fmi_t before=fmi;CHECK(!s5l8920_fmi_write(&fmi,o,v));CHECK(!memcmp(&before,&fmi,sizeof before));
}
static void reject_read(uint32_t o) {
    uint32_t v=0xdeadbeef;s5l8920_fmi_t before=fmi;
    CHECK(!s5l8920_fmi_read(&fmi,o,&v) && v==0xdeadbeef);CHECK(!memcmp(&before,&fmi,sizeof before));
}
static void configure(unsigned chip,uint8_t address) {
    wr(0x400,1);wr(0x408,0x12345678);wr(0x40c,1u<<chip);wr(0x414,0x90);
    wr(0x418,address);wr(0x420,0);
}
static void test_configuration(void) {
    init();uint32_t v=123;
    CHECK(!s5l8920_fmi_init(NULL));CHECK(!s5l8920_fmi_read(NULL,0,&v));
    CHECK(!s5l8920_fmi_write(NULL,0,0));CHECK(!s5l8920_fmi_service(NULL,NULL,NULL));
    CHECK(!s5l8920_fmi_reset_line(NULL,true));
    reject(0x40c,1);reject_read(0x444);reject(0x400,0);reject(0x400,2);wr(0x400,1);
    CHECK(rd(0x40c)==0 && rd(0x444)==0);reject_read(0x408);reject_read(0x448);
    CHECK(!s5l8920_fmi_read(&fmi,0x444,NULL));
    const uint32_t unknown[]={0,0x404,0x40d,0x41c,0x428,0x440,0x44c,0x814,UINT32_MAX};
    for(unsigned n=0;n<sizeof unknown/sizeof unknown[0];n++) { reject(unknown[n],0);reject_read(unknown[n]); }
    reject_read(0x400);reject_read(0x410);reject_read(0x414);reject_read(0x418);reject_read(0x420);reject_read(0x424);
    reject(0x40c,3);reject(0x40c,256);reject(0x414,0x70);reject(0x414,0);reject(0x418,256);
    reject(0x420,1);reject(0x424,1);reject(0x444,2);reject(0x410,0xb);reject(0x410,9);
    wr(0x414,0x90);wr(0x40c,1);reject(0x410,9);wr(0x418,0);reject(0x410,9);
    wr(0x420,0);reject(0x410,1);wr(0x40c,0);
    s5l8920_fmi_t before=fmi;wr(0x400,1);CHECK(!memcmp(&before,&fmi,sizeof before));
}
static void test_unselected_bus(void) {
    init();configure(0,0);wr(0x40c,0);wr(0x410,9);
    s5l8920_fmi_t before=fmi;
    CHECK(!s5l8920_fmi_service(&fmi,device,&endpoint));CHECK(!memcmp(&before,&fmi,sizeof before));
    CHECK(!fmi.status && !fmi.id_valid && fmi.pending);
    endpoint.idle_bus=true;CHECK(s5l8920_fmi_service(&fmi,device,&endpoint));
    CHECK(endpoint.last_chip==S5L8920_FMI_NO_CHIP && rd(0x444)==9);wr(0x444,9);wr(0x408,0);wr(0x424,0);
    for(unsigned n=0;n<8;n++) { wr(0x410,0x50);CHECK(s5l8920_fmi_service(&fmi,NULL,NULL));CHECK(rd(0x448)==0xff);wr(0x410,0); }
    wr(0x414,0xff);wr(0x410,1);endpoint.dirty=true;
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint));CHECK(endpoint.dirty && endpoint.resets==0 && fmi.commands==2 && rd(0x444)==1);
}
static void test_timing_before_mode(void) {
    init();reject_read(0x408);wr(0x408,0xffff);
    CHECK(!fmi.enabled && fmi.timing==0xffff && !fmi.pending && !endpoint.calls);
    reject_read(0x408);reject_read(0x444);reject(0x414,0xff);reject(0x410,1);
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint) && !endpoint.calls);
    wr(0x400,1);CHECK(rd(0x408)==0xffff && rd(0x444)==0);
    CHECK(s5l8920_fmi_reset_line(&fmi,true));reject(0x408,0xffff);
    CHECK(s5l8920_fmi_reset_line(&fmi,false));wr(0x400,1);reject_read(0x408);
    wr(0x408,0x12345678);CHECK(rd(0x408)==0x12345678);
}
static void test_data(void) {
    for(unsigned chip=0;chip<8;chip++) {
        init();uint8_t address=chip&1?0x20:0;configure(chip,address);wr(0x410,9);
        s5l8920_fmi_t pending=fmi;
        for(unsigned n=0;n<10;n++)CHECK(rd(0x444)==0);
        CHECK(!memcmp(&pending,&fmi,sizeof pending) && !endpoint.calls);
        reject(0x40c,0);reject(0x414,0xff);reject(0x408,0);reject(0x410,0);reject(0x400,1);
        CHECK(!s5l8920_fmi_service(&fmi,NULL,NULL));CHECK(!memcmp(&pending,&fmi,sizeof pending));
        endpoint.refuse=true;CHECK(!s5l8920_fmi_service(&fmi,device,&endpoint));
        CHECK(!memcmp(&pending,&fmi,sizeof pending));endpoint.refuse=false;endpoint.reenter=true;
        CHECK(s5l8920_fmi_service(&fmi,device,&endpoint));
        CHECK(endpoint.calls==2 && endpoint.last_chip==chip && endpoint.last_address==address);
        CHECK(fmi.commands==1 && rd(0x444)==9);wr(0x444,1);CHECK(rd(0x444)==8);wr(0x444,8);CHECK(rd(0x444)==0);
        reject_read(0x448);reject(0x410,0x50);wr(0x408,0);reject(0x410,0x50);wr(0x424,0);
        for(unsigned n=0;n<8;n++) {
            wr(0x410,0x50);reject_read(0x448);reject(0x410,0);reject(0x40c,0);
            CHECK(s5l8920_fmi_service(&fmi,NULL,NULL));
            unsigned expected=(uint8_t)(endpoint.ids[chip][n]^address);
            CHECK(rd(0x448)==expected && rd(0x448)==expected && fmi.cursor==n+1);
            reject(0x410,0x50);reject(0x408,1);wr(0x410,0);reject_read(0x448);
        }
        CHECK(fmi.bytes_latched==8 && endpoint.calls==2 && fmi.commands==1);reject(0x410,0x50);
        wr(0x408,0x12345678);CHECK(rd(0x408)==0x12345678);wr(0x40c,0);
        CHECK(!fmi.id_valid && !fmi.data_valid && !fmi.cursor);
        s5l8920_fmi_t before=fmi;CHECK(s5l8920_fmi_service(&fmi,NULL,NULL));CHECK(!memcmp(&before,&fmi,sizeof before));
    }
}
static void test_reset(void) {
    init();configure(7,0);wr(0x410,9);CHECK(s5l8920_fmi_service(&fmi,device,&endpoint));wr(0x444,9);
    endpoint.dirty=true;wr(0x414,0xff);wr(0x410,1);CHECK(endpoint.dirty && !endpoint.resets && rd(0x444)==0);
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint));CHECK(!endpoint.dirty && endpoint.resets==1 && rd(0x444)==1 && !fmi.id_valid);
    wr(0x444,1);wr(0x414,0x90);wr(0x410,9);CHECK(s5l8920_fmi_reset_line(&fmi,true));
    CHECK(fmi.reset_asserted && !fmi.enabled && !fmi.pending && !fmi.programmed && !fmi.status);
    CHECK(fmi.commands==2 && endpoint.calls==2);reject(0x400,1);reject_read(0x444);
    s5l8920_fmi_t before=fmi;CHECK(s5l8920_fmi_reset_line(&fmi,true));CHECK(!memcmp(&before,&fmi,sizeof before));
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint) && endpoint.calls==2);
    CHECK(s5l8920_fmi_reset_line(&fmi,false));reject_read(0x444);wr(0x400,1);reject_read(0x408);
    CHECK(s5l8920_fmi_init(&fmi));CHECK(!fmi.commands && !fmi.bytes_latched && !fmi.enabled);
    configure(0,0);fmi.commands=UINT64_MAX;reject(0x410,9);fmi.commands=0;wr(0x410,9);
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint));wr(0x444,9);wr(0x408,0);wr(0x424,0);
    fmi.bytes_latched=UINT64_MAX;reject(0x410,0x50);
}
static uint32_t reg(unsigned bank,uint32_t off) { return S5L8920_FMI_BASE+S5L8920_FMI_STRIDE*bank+off; }
static uint32_t gate(unsigned bank) { return S5L8920_CLOCK_GATE_BASE+4*(S5L8920_FMI_GATE+bank); }
static void bw(unsigned bank,uint32_t off,uint32_t v) { board.bus.write32(&board,reg(bank,off),v);CHECK(!board.bus_failure.reason); }
static void test_board(void) {
    init();CHECK(s5l8920_init(&board));if(!board.ram)return;
    board.bus.write32(&board,reg(0,0x40c),1);CHECK(board.bus_failure.reason==S5L8920_BUS_UNMAPPED);
    s5l8920_clear_bus_failure(&board);CHECK(!s5l8920_fmi_attach(&board,0,&fmi));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_FMI_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_FMI_GATE+1,15));
    CHECK(!s5l8920_fmi_attach(&board,2,&fmi));CHECK(s5l8920_fmi_attach(&board,0,&fmi));
    CHECK(!s5l8920_fmi_attach(&board,1,&fmi));CHECK(s5l8920_fmi_attach(&board,1,&other));
    CHECK(!s5l8920_fmi_attach(&board,0,&other));CHECK(s5l8920_fmi_attach(&board,0,&fmi));
    s5l8920_fmi_t cold=fmi;
    board.bus.write32(&board,gate(0),0);board.bus.write32(&board,reg(0,0x408),0xffff);
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&cold,&fmi,sizeof cold));
    s5l8920_clear_bus_failure(&board);board.bus.write32(&board,gate(0),15);
    bw(0,0x408,0xffff);bw(1,0x408,0x1234);
    CHECK(!fmi.enabled && !other.enabled && fmi.timing==0xffff && other.timing==0x1234);
    s5l8920_clear_bus_failure(&board);
    bw(0,0x400,1);bw(1,0x400,1);bw(1,0x40c,128);CHECK(board.bus.read32(&board,reg(0,0x40c))==0);
    bw(0,0x408,0);bw(0,0x40c,2);bw(0,0x414,0x90);bw(0,0x418,0);bw(0,0x420,0);bw(0,0x410,9);
    s5l8920_fmi_t before=fmi;board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;board.timebase_ticks=123;
    arm_cpu_t cpu=board.cpu;
    board.bus.write32(&board,gate(0),0);CHECK(!board.bus_failure.reason);
    CHECK(s5l8920_fmi_board_service(&board,0,device,&endpoint));CHECK(!memcmp(&before,&fmi,sizeof before));
    CHECK(!endpoint.calls && board.timebase_ticks==123 && !memcmp(&cpu,&board.cpu,sizeof cpu));
    (void)board.bus.read32(&board,reg(0,0x444));CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED);
    CHECK(!s5l8920_fmi_board_service(&board,0,device,&endpoint));s5l8920_clear_bus_failure(&board);
    board.bus.write32(&board,gate(0),15);endpoint.board=&board;endpoint.reenter=true;
    CHECK(s5l8920_fmi_board_service(&board,0,device,&endpoint));
    CHECK(fmi.commands==1 && !other.commands && endpoint.calls==1 && !board.bus_failure.reason);
    CHECK(!memcmp(&cpu,&board.cpu,sizeof cpu) && board.timebase_ticks==123);
    before=fmi;board.bus.write16(&board,reg(0,0x40c),0);
    CHECK(board.bus_failure.reason==S5L8920_BUS_ACCESS_UNIMPLEMENTED && !memcmp(&before,&fmi,sizeof before));
    s5l8920_clear_bus_failure(&board);board.bus.write32(&board,reg(0,0x40d),0);
    CHECK(board.bus_failure.reason==S5L8920_BUS_ACCESS_UNIMPLEMENTED && !memcmp(&before,&fmi,sizeof before));
    s5l8920_clear_bus_failure(&board);board.bus.write32(&board,reg(0,0x440),0);
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&before,&fmi,sizeof before));
    s5l8920_clear_bus_failure(&board);board.bus.write32(&board,gate(0),31);
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&before,&fmi,sizeof before));
    s5l8920_clear_bus_failure(&board);board.bus.write32(&board,gate(0),0x8000000f);
    CHECK(!board.bus_failure.reason && fmi.reset_asserted && !fmi.enabled && !fmi.id_valid && other.enabled);
    board.bus.write32(&board,gate(0),15);CHECK(!board.bus_failure.reason && !fmi.reset_asserted);
    CHECK(!s5l8920_fmi_board_service(&board,2,device,&endpoint));
    s5l8920_fmi_t other_before=other;before=fmi;
    CHECK(s5l8920_reset(&board));CHECK(!board.fmi[0] && !board.fmi[1]);
    CHECK(!memcmp(&before,&fmi,sizeof before) && !memcmp(&other_before,&other,sizeof other));
    CHECK(!s5l8920_fmi_attach(&board,1,&other));CHECK(s5l8920_fmi_attach(&board,0,&fmi));
    before=fmi;s5l8920_free(&board);CHECK(!board.ram && !memcmp(&before,&fmi,sizeof before));
}
int main(void) {
    test_configuration();test_data();test_unselected_bus();test_timing_before_mode();test_reset();test_board();s5l8920_free(&board);
    printf("s5l8920_fmi: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
