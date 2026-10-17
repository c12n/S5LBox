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
        CHECK(!s5l8920_fmi_write(e->controller,0x814,0));
        CHECK(!s5l8920_fmi_write(e->controller,0x418,0x34560000));
        CHECK(!s5l8920_fmi_write(e->controller,0x41c,0x12));
        CHECK(!s5l8920_fmi_write(e->controller,0x420,4));
        CHECK(!s5l8920_fmi_read(e->controller,0x418,&value) && value==0xa5a5a5a5);
        CHECK(!s5l8920_fmi_read(e->controller,0x41c,&value) && value==0xa5a5a5a5);
        CHECK(!s5l8920_fmi_read(e->controller,0x420,&value) && value==0xa5a5a5a5);
        CHECK(!s5l8920_fmi_read(e->controller,0x814,&value) && value==0xa5a5a5a5);
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
    const uint32_t unknown[]={0,0x404,0x40d,0x41d,0x428,0x441,0x44d,0x808,0x810,0x818,UINT32_MAX};
    for(unsigned n=0;n<sizeof unknown/sizeof unknown[0];n++) { reject(unknown[n],0);reject_read(unknown[n]); }
    reject_read(0x400);reject_read(0x410);reject_read(0x414);reject_read(0x418);reject_read(0x420);reject_read(0x424);
    reject(0x40c,3);reject(0x40c,256);reject(0x414,0x71);reject(0x414,0x30);reject(0x418,256);
    reject(0x420,1);reject(0x424,1);reject(0x444,4);reject(0x410,0xb);reject(0x410,9);
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
static void test_ecc_mask(void) {
    init();reject_read(0x814);reject(0x814,0);wr(0x400,1);reject_read(0x814);
    reject(0x814,1);reject(0x814,0x1a8);reject(0x814,UINT32_MAX);wr(0x814,0);
    CHECK(rd(0x814)==0 && !fmi.pending && !fmi.status && !fmi.commands && !fmi.bytes_latched);
    s5l8920_fmi_t before=fmi;wr(0x814,0);
    CHECK(!memcmp(&before,&fmi,sizeof before));reject_read(0x810);
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint) && !endpoint.calls);
    CHECK(s5l8920_fmi_reset_line(&fmi,true));reject(0x814,0);reject_read(0x814);
    CHECK(s5l8920_fmi_reset_line(&fmi,false));wr(0x400,1);reject_read(0x814);
    wr(0x814,0);CHECK(rd(0x814)==0);CHECK(s5l8920_fmi_init(&fmi));
    wr(0x400,1);reject_read(0x814);
}
static void test_page_address_staging(void) {
    init();
    const uint32_t offsets[]={0x418,0x41c,0x420};
    for(unsigned n=0;n<3;n++) { reject_read(offsets[n]);reject(offsets[n],0); }
    wr(0x400,1);
    for(unsigned n=0;n<3;n++)reject_read(offsets[n]);
    wr(0x41c,0x12);wr(0x418,0x34560000);wr(0x420,4);
    CHECK(rd(0x41c)==0x12 && rd(0x418)==0x34560000 && rd(0x420)==4);
    reject(0x41c,256);reject(0x418,0x34560001);reject(0x420,1);reject(0x420,5);
    CHECK(!fmi.pending && !fmi.status && !fmi.commands && !fmi.bytes_latched);
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint) && !endpoint.calls);
    wr(0x414,0x3000);reject(0x410,0xb); /* No selected chip or explicit timing. */
    wr(0x414,0x90);wr(0x40c,1);reject(0x410,9);
    wr(0x420,0);reject(0x410,9); /* Cannot truncate staged row bits into READID. */
    wr(0x418,0);wr(0x420,4);reject(0x410,9); /* Even page zero has five address cycles. */
    wr(0x418,0x20);wr(0x420,0);wr(0x410,9);
    for(unsigned n=0;n<3;n++)reject(offsets[n],0);
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint));
    CHECK(endpoint.calls==1 && endpoint.last_address==0x20 && rd(0x444)==9);
    wr(0x444,9);wr(0x408,0);wr(0x424,0);wr(0x410,0x50);
    CHECK(s5l8920_fmi_service(&fmi,NULL,NULL));
    for(unsigned n=0;n<3;n++)reject(offsets[n],0);
    wr(0x410,0);
    CHECK(s5l8920_fmi_reset_line(&fmi,true));
    for(unsigned n=0;n<3;n++) { reject_read(offsets[n]);reject(offsets[n],0); }
    CHECK(s5l8920_fmi_reset_line(&fmi,false));wr(0x400,1);
    for(unsigned n=0;n<3;n++)reject_read(offsets[n]);
    wr(0x41c,0xff);wr(0x418,0xffff0000);wr(0x420,4);
    CHECK(rd(0x41c)==0xff && rd(0x418)==0xffff0000 && rd(0x420)==4);
    CHECK(s5l8920_fmi_init(&fmi));wr(0x400,1);
    for(unsigned n=0;n<3;n++)reject_read(offsets[n]);
}
static void test_page_command_queue(void) {
    init();wr(0x400,1);wr(0x408,0x11111);wr(0x40c,4);
    wr(0x41c,0x12);wr(0x418,0x34560000);wr(0x420,4);wr(0x414,0x3000);
    wr(0x410,0xb);
    CHECK(fmi.pending && !fmi.status && !fmi.commands && !fmi.id_valid);
    s5l8920_fmi_t before=fmi;
    CHECK(!s5l8920_fmi_service(&fmi,NULL,NULL));
    CHECK(!memcmp(&before,&fmi,sizeof before));
    CHECK(rd(0x444)==0);reject_read(0x448);
    CHECK(s5l8920_fmi_reset_line(&fmi,true));
    CHECK(!fmi.pending && !fmi.status && !fmi.commands);
}
/* Sparse explicit backing: each row has 64 data bytes and four spare bytes.
 * These are unit-test geometry and bytes, not an installed guest NAND image. */
static const uint32_t backed_rows[]={0,0xffff,0x10000,0xffffff};
static struct {
    uint8_t backing[8][4][68],cache[68];
    uint32_t row;
    unsigned chip,reads;
    bool busy,refuse,reenter;
    s5l8920_fmi_t *controller;
    s5l8920_t *board;
} page_device;
static unsigned page_attempts;
static bool read_page(void *owner,unsigned chip,uint32_t row) {
    CHECK(owner==&page_device);page_attempts++;
    unsigned index=0;while(index<4 && backed_rows[index]!=row)index++;
    if(page_device.refuse || chip>=8 || index==4)return false;
    if(page_device.reenter) {
        s5l8920_fmi_t *f=page_device.controller,before=*f;uint32_t v=0x12345678;
        s5l8920_fmi_endpoint_t capability={NULL,read_page};
        CHECK(!s5l8920_fmi_init(f));CHECK(!s5l8920_fmi_reset_line(f,true));
        CHECK(!s5l8920_fmi_write(f,0x410,0));CHECK(!s5l8920_fmi_write(f,0x444,0xb));
        CHECK(!s5l8920_fmi_write(f,0x418,0));CHECK(!s5l8920_fmi_write(f,0x414,0xff));
        CHECK(!s5l8920_fmi_read(f,0x444,&v) && v==0x12345678);
        CHECK(!s5l8920_fmi_service_endpoint(f,&capability,owner));
        CHECK(!s5l8920_fmi_service(f,NULL,NULL));CHECK(!memcmp(&before,f,sizeof before));
        if(page_device.board) {
            s5l8920_t *m=page_device.board;uint8_t *ram=m->ram;
            CHECK(!s5l8920_reset(m));s5l8920_free(m);CHECK(m->ram==ram);
            CHECK(!s5l8920_fmi_attach(m,0,f));
            CHECK(!s5l8920_fmi_board_service_endpoint(m,0,&capability,owner));
            CHECK(!s5l8920_fmi_board_service_endpoint(m,1,&capability,owner));
            CHECK(!s5l8920_fmi_board_service(m,0,NULL,NULL));
        }
    }
    memcpy(page_device.cache,page_device.backing[chip][index],sizeof page_device.cache);
    page_device.chip=chip;page_device.row=row;page_device.reads++;page_device.busy=true;
    return true;
}
static void init_pages(void) {
    memset(&page_device,0,sizeof page_device);page_device.controller=&fmi;page_attempts=0;
    memset(page_device.cache,0xa5,sizeof page_device.cache);
    for(unsigned c=0;c<8;c++)for(unsigned p=0;p<4;p++)for(unsigned b=0;b<68;b++)
        page_device.backing[c][p][b]=(uint8_t)(c*23+p*37+b*11);
}
static void page_configure(s5l8920_fmi_t *f,unsigned chip,uint32_t row) {
    CHECK(s5l8920_fmi_write(f,0x400,1));CHECK(s5l8920_fmi_write(f,0x408,0x11111));
    CHECK(s5l8920_fmi_write(f,0x40c,1u<<chip));CHECK(s5l8920_fmi_write(f,0x414,0x3000));
    CHECK(s5l8920_fmi_write(f,0x418,(row&0xffff)<<16));
    CHECK(s5l8920_fmi_write(f,0x41c,row>>16));CHECK(s5l8920_fmi_write(f,0x420,4));
}
static void test_page_commands(void) {
    const s5l8920_fmi_endpoint_t capability={NULL,read_page},missing={NULL,NULL};
    CHECK(!s5l8920_fmi_service_endpoint(NULL,&capability,&page_device));
    for(unsigned c=0;c<8;c++)for(unsigned p=0;p<4;p++) {
        init();init_pages();page_configure(&fmi,c,backed_rows[p]);wr(0x410,0xb);
        s5l8920_fmi_t before=fmi;
        for(unsigned n=0;n<4;n++)CHECK(rd(0x444)==0);
        CHECK(!s5l8920_fmi_service_endpoint(&fmi,NULL,NULL));
        CHECK(!s5l8920_fmi_service_endpoint(&fmi,&missing,NULL));
        CHECK(!s5l8920_fmi_service(&fmi,device,&endpoint) && !endpoint.calls);
        page_device.refuse=true;
        CHECK(!s5l8920_fmi_service_endpoint(&fmi,&capability,&page_device));
        CHECK(!memcmp(&before,&fmi,sizeof before) && !page_device.reads && !page_device.busy);
        for(unsigned n=0;n<sizeof page_device.cache;n++)CHECK(page_device.cache[n]==0xa5);
        reject(0x40c,0);reject(0x414,0xff);reject(0x410,0);reject(0x408,0);
        reject(0x418,0);reject(0x41c,0);reject(0x420,0);reject(0x814,0);
        wr(0x444,0xb);CHECK(rd(0x444)==0); /* Ack before service cannot finish work. */
        page_device.refuse=false;page_device.reenter=true;
        CHECK(s5l8920_fmi_service_endpoint(&fmi,&capability,&page_device));
        CHECK(page_attempts==2 && page_device.reads==1 && page_device.chip==c && page_device.row==backed_rows[p]);
        CHECK(!memcmp(page_device.cache,page_device.backing[c][p],sizeof page_device.cache));
        CHECK(page_device.busy && fmi.commands==1 && rd(0x444)==0xb && !fmi.pending);
        CHECK(!fmi.id_valid && !fmi.data_valid && !fmi.bytes_latched);reject_read(0x448);
        before=fmi;CHECK(s5l8920_fmi_service_endpoint(&fmi,&capability,&page_device));
        CHECK(page_attempts==2 && !memcmp(&before,&fmi,sizeof before));reject(0x410,0xb);
        wr(0x444,2);CHECK(rd(0x444)==9);wr(0x444,1);CHECK(rd(0x444)==8);wr(0x444,8);CHECK(rd(0x444)==0);
        wr(0x408,0);wr(0x424,0);reject(0x410,0x50);wr(0x414,0x70);wr(0x44c,0x4040);reject(0x410,0x50);
        CHECK(s5l8920_fmi_reset_line(&fmi,true));CHECK(page_device.reads==1 && page_device.busy);
    }
    init();init_pages();page_configure(&fmi,0,1);wr(0x410,0xb);s5l8920_fmi_t before=fmi;
    CHECK(!s5l8920_fmi_service_endpoint(&fmi,&capability,&page_device));
    CHECK(!memcmp(&before,&fmi,sizeof before) && page_attempts==1 && !page_device.reads);
    CHECK(s5l8920_fmi_reset_line(&fmi,true));
    CHECK(s5l8920_fmi_service_endpoint(&fmi,&capability,&page_device) && page_attempts==1);
    CHECK(s5l8920_fmi_reset_line(&fmi,false));
    page_configure(&fmi,0,0);fmi.commands=UINT64_MAX;reject(0x410,0xb);
    fmi.commands=UINT64_MAX-1;wr(0x410,0xb);
    CHECK(s5l8920_fmi_service_endpoint(&fmi,&capability,&page_device) && fmi.commands==UINT64_MAX);
    wr(0x444,0xb);reject(0x410,0xb);
    /* Each required programmed field, command/control pairing and zero column. */
    for(unsigned omit=0;omit<4;omit++) {
        init();wr(0x400,1);wr(0x40c,1);wr(0x414,0x3000);
        if(omit!=0)wr(0x408,0);
        if(omit!=1)wr(0x418,0);
        if(omit!=2)wr(0x41c,0);
        if(omit!=3)wr(0x420,4);
        reject(0x410,0xb);
    }
    init();page_configure(&fmi,0,0);reject(0x410,1);reject(0x410,9);
    wr(0x40c,0);reject(0x410,0xb);wr(0x40c,1);wr(0x418,0x20);reject(0x410,0xb);
    wr(0x418,0);wr(0x420,0);reject(0x410,0xb);wr(0x420,4);
    wr(0x414,0xff);reject(0x410,0xb);wr(0x414,0x90);reject(0x410,0xb);
    reject(0x414,0xd060);reject(0x414,0x300000);reject(0x414,0x3001);
    init();init_pages();configure(0,0);wr(0x410,9);
    CHECK(s5l8920_fmi_service(&fmi,device,&endpoint) && fmi.id_valid);wr(0x444,9);
    page_configure(&fmi,0,0);wr(0x410,0xb);CHECK(!fmi.id_valid && !fmi.data_valid && !fmi.cursor);
    CHECK(s5l8920_fmi_reset_line(&fmi,true));
    CHECK(s5l8920_fmi_service_endpoint(&fmi,&capability,&page_device) && !page_attempts);
    CHECK(s5l8920_fmi_reset_line(&fmi,false));wr(0x400,1);reject(0x410,0xb);
    reject_read(0x418);reject_read(0x41c);reject_read(0x420);
}
static void test_status_setup_queue(void) {
    init();wr(0x400,1);wr(0x408,0x11111);wr(0x40c,1);
    wr(0x44c,0x4040);wr(0x414,0x70);wr(0x410,1);
    CHECK(fmi.pending && !fmi.commands && !fmi.status);
    s5l8920_fmi_t before=fmi;
    CHECK(!s5l8920_fmi_service(&fmi,NULL,NULL));
    CHECK(!memcmp(&before,&fmi,sizeof before));
    CHECK(s5l8920_fmi_reset_line(&fmi,true));
}
typedef struct {
    s5l8920_fmi_t *controller;
    s5l8920_t *board;
    uint8_t backing[68],cache[68];
    unsigned chip,copied,commands,samples;
    bool page,mode,refuse_command,refuse_sample,reenter;
} status_fixture_t;
static status_fixture_t status_fixture;
static bool status_command(void *owner,unsigned chip,uint8_t command);
static bool status_sample(void *owner,unsigned chip,uint8_t *value);
static const s5l8920_fmi_status_device_t status_ops={status_command,status_sample};
static void status_reentry(void) {
    if(!status_fixture.reenter)return;
    s5l8920_fmi_t *f=status_fixture.controller,before=*f;uint32_t v=0x12345678;
    CHECK(!s5l8920_fmi_init(f));CHECK(!s5l8920_fmi_reset_line(f,true));
    CHECK(!s5l8920_fmi_write(f,0x410,0));CHECK(!s5l8920_fmi_write(f,0x444,0x31ffff));
    CHECK(!s5l8920_fmi_write(f,0x44c,0x4040));CHECK(!s5l8920_fmi_write(f,0x440,0));
    CHECK(!s5l8920_fmi_write(f,0x10,0));CHECK(!s5l8920_fmi_write(f,0xc,0xf));
    CHECK(!s5l8920_fmi_read(f,0x448,&v) && v==0x12345678);
    CHECK(!s5l8920_fmi_read(f,0x44c,&v) && v==0x12345678);
    CHECK(!s5l8920_fmi_service_io(f,NULL,&status_ops,&status_fixture));
    CHECK(!memcmp(&before,f,sizeof before));
    if(status_fixture.board) {
        s5l8920_t *m=status_fixture.board;uint8_t *ram=m->ram;
        CHECK(!s5l8920_reset(m));s5l8920_free(m);CHECK(m->ram==ram);
        CHECK(!s5l8920_fmi_attach(m,0,f));
        CHECK(!s5l8920_fmi_board_service_io(m,0,NULL,&status_ops,&status_fixture));
        CHECK(!s5l8920_fmi_board_service_io(m,1,NULL,&status_ops,&status_fixture));
    }
}
static bool status_page(void *owner,unsigned chip,uint32_t row) {
    CHECK(owner==&status_fixture);
    if(chip!=status_fixture.chip || row!=0x10203)return false;
    status_fixture.page=true;status_fixture.mode=false;status_fixture.copied=0;
    memset(status_fixture.cache,0xa5,sizeof status_fixture.cache);return true;
}
static bool status_command(void *owner,unsigned chip,uint8_t command) {
    CHECK(owner==&status_fixture);status_reentry();
    if(status_fixture.refuse_command || chip!=status_fixture.chip)return false;
    if(command==0) {
        if(!status_fixture.mode || !status_fixture.page || status_fixture.copied!=sizeof status_fixture.cache)return false;
        status_fixture.mode=false;
    }else if(command==0x70)status_fixture.mode=true;
    else { CHECK(false);return false; }
    status_fixture.commands++;return true;
}
static bool status_sample(void *owner,unsigned chip,uint8_t *value) {
    CHECK(owner==&status_fixture);status_reentry();
    if(status_fixture.refuse_sample || chip!=status_fixture.chip || !status_fixture.mode) {
        *value=0xff;return false; /* A refused sample cannot leak an output byte. */
    }
    *value=(uint8_t)(0x80u|(status_fixture.page && status_fixture.copied==sizeof status_fixture.cache?0x40u:0));
    status_fixture.samples++;return true;
}
static void status_progress(void) {
    CHECK(status_fixture.page && status_fixture.copied<sizeof status_fixture.cache);
    unsigned count=17;
    memcpy(status_fixture.cache+status_fixture.copied,status_fixture.backing+status_fixture.copied,count);
    status_fixture.copied+=count;
}
static void status_init(unsigned chip) {
    memset(&status_fixture,0,sizeof status_fixture);status_fixture.controller=&fmi;
    status_fixture.chip=chip;status_fixture.reenter=true;
    for(unsigned n=0;n<sizeof status_fixture.backing;n++)status_fixture.backing[n]=(uint8_t)(chip*19+n*37);
}
static void status_setup(s5l8920_fmi_t *f) {
    CHECK(s5l8920_fmi_write(f,0x44c,0x4040));CHECK(s5l8920_fmi_write(f,0x414,0x70));
    CHECK(s5l8920_fmi_write(f,0x410,1));
    CHECK(s5l8920_fmi_service_io(f,NULL,&status_ops,&status_fixture));
    CHECK(s5l8920_fmi_write(f,0x444,1));CHECK(s5l8920_fmi_write(f,0x440,0));
    CHECK(s5l8920_fmi_write(f,0x10,0));CHECK(s5l8920_fmi_write(f,0x444,0x31ffff));
    CHECK(s5l8920_fmi_write(f,0xc,0xf));CHECK(s5l8920_fmi_write(f,0x424,0));
}
static void test_status_polling(void) {
    const s5l8920_fmi_endpoint_t pages_cap={NULL,status_page};
    const s5l8920_fmi_status_device_t no_command={NULL,status_sample},no_sample={status_command,NULL};
    CHECK(!s5l8920_fmi_service_io(NULL,NULL,NULL,NULL));
    for(unsigned c=0;c<8;c++) {
        init();status_init(c);page_configure(&fmi,c,0x10203);wr(0x410,0xb);
        CHECK(s5l8920_fmi_service_endpoint(&fmi,&pages_cap,&status_fixture));wr(0x444,0xb);
        CHECK(fmi.page_issued && !fmi.status_mode && !status_fixture.copied);
        wr(0x44c,0x4040);CHECK(rd(0x44c)==0x4040);wr(0x414,0x70);wr(0x410,1);
        s5l8920_fmi_t before=fmi;
        CHECK(!s5l8920_fmi_service_endpoint(&fmi,&pages_cap,&status_fixture));
        CHECK(!s5l8920_fmi_service_io(&fmi,NULL,&no_command,&status_fixture));
        status_fixture.refuse_command=true;status_fixture_t device_before=status_fixture;
        CHECK(!s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
        CHECK(!memcmp(&device_before,&status_fixture,sizeof device_before) && !memcmp(&before,&fmi,sizeof before));
        status_fixture.refuse_command=false;
        CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
        CHECK(fmi.status_mode && status_fixture.mode && fmi.page_issued && rd(0x444)==1 && fmi.commands==2);
        wr(0x444,1);wr(0x440,0);wr(0x10,0);wr(0x444,0x31ffff);wr(0xc,0xf);wr(0x424,0);
        CHECK(rd(0x440)==0 && rd(0x10)==0);reject_read(0xc);wr(0x410,0x50);reject_read(0x448);
        CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
        CHECK(fmi.pending && fmi.status_samples==1 && rd(0x448)==0x80 && rd(0x444)==0);
        before=fmi;status_fixture.refuse_sample=true;device_before=status_fixture;
        CHECK(!s5l8920_fmi_service_io(&fmi,NULL,&no_sample,&status_fixture));
        CHECK(!s5l8920_fmi_service_endpoint(&fmi,&pages_cap,&status_fixture));
        CHECK(!s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
        CHECK(!memcmp(&device_before,&status_fixture,sizeof device_before) && !memcmp(&before,&fmi,sizeof before));
        CHECK(rd(0x448)==0x80 && rd(0x444)==0);status_fixture.refuse_sample=false;
        reject(0x40c,0);reject(0x414,0);reject(0x408,0);reject(0x44c,0x4040);
        reject(0x440,0);reject(0x10,0);reject(0xc,0xf);reject(0x410,0x50);
        for(unsigned block=0;block<4;block++) {
            before=fmi;status_progress();
            for(unsigned n=0;n<3;n++)CHECK(rd(0x444)==0 && rd(0x448)==0x80);
            CHECK(!memcmp(&before,&fmi,sizeof before));
            CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
            CHECK(rd(0x444)==(block==3?0x20u:0u) && rd(0x448)==(block==3?0xc0u:0x80u));
        }
        CHECK(!memcmp(status_fixture.cache,status_fixture.backing,sizeof status_fixture.cache));
        CHECK(!fmi.pending && fmi.status_samples==5 && !fmi.bytes_latched && status_fixture.samples==5);
        before=fmi;CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
        CHECK(!memcmp(&before,&fmi,sizeof before) && status_fixture.samples==5);reject(0x414,0);
        wr(0x410,0);reject_read(0x448);CHECK(rd(0x444)==0x20);
        wr(0x414,0);wr(0x410,0);wr(0x410,1);
        CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
        CHECK(!status_fixture.mode && !fmi.status_mode && fmi.page_issued && rd(0x444)==0x21 && fmi.commands==3);
        wr(0x444,1);CHECK(rd(0x444)==0x20);wr(0x444,0x31ffff);CHECK(rd(0x444)==0);
        CHECK(status_fixture.commands==2);reject(0x410,0x50);
        CHECK(s5l8920_fmi_reset_line(&fmi,true));CHECK(fmi.status_samples==5 && !fmi.page_issued && !fmi.status_mode);
        CHECK(status_fixture.copied==68 && status_fixture.page); /* Controller reset is not a chip reset. */
    }
}
static void test_status_guards(void) {
    init();status_init(0);reject(0x44c,0x4040);reject(0x440,0);reject(0x10,0);reject(0xc,0xf);
    wr(0x400,1);reject_read(0x44c);reject_read(0x440);reject_read(0x10);reject_read(0xc);
    reject(0x44c,0);reject(0x44c,0x40);reject(0x44c,0x8080);reject(0x44c,0x14040);
    reject(0x440,0x20);reject(0x10,0x100);reject(0xc,0);reject(0xc,0xf);
    wr(0x440,0);reject(0xc,0xf);wr(0x10,0);wr(0xc,0xf);reject(0xc,0x100);
    wr(0x444,0x31ffff);reject(0x444,0x32ffff);CHECK(!fmi.commands && !fmi.status_samples);
    wr(0x408,0x11111);wr(0x40c,1);status_setup(&fmi);wr(0x410,0x50);
    CHECK(fmi.pending && !fmi.data_valid);wr(0x410,0);CHECK(!fmi.pending && !fmi.data_valid);
    CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture) && !status_fixture.samples);
    wr(0x410,0x50);CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
    CHECK(fmi.pending && rd(0x448)==0x80);wr(0x410,0);reject_read(0x448);
    wr(0x414,0);reject(0x410,1); /* No prior page read to resume. */
    wr(0x408,0x111111);reject(0x410,0x50);wr(0x408,0x11111);
    fmi.status_samples=UINT64_MAX;reject(0x410,0x50);fmi.status_samples=UINT64_MAX-1;
    wr(0x410,0x50);CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
    s5l8920_fmi_t before=fmi;unsigned samples=status_fixture.samples;
    CHECK(!s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));
    CHECK(!memcmp(&before,&fmi,sizeof before) && status_fixture.samples==samples);wr(0x410,0);
    CHECK(s5l8920_fmi_reset_line(&fmi,true));reject(0x44c,0x4040);reject(0x10,0);reject(0x440,0);reject(0xc,0xf);
    CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture) && status_fixture.samples==samples);
    CHECK(s5l8920_fmi_reset_line(&fmi,false));wr(0x400,1);
    reject_read(0x44c);reject_read(0x440);reject_read(0x10);CHECK(fmi.status_samples==UINT64_MAX);
    CHECK(s5l8920_fmi_init(&fmi) && !fmi.status_samples);
    for(unsigned omit=0;omit<4;omit++) {
        init();status_init(0);wr(0x400,1);wr(0x408,0x11111);wr(0x40c,1);
        wr(0x414,0x70);wr(0x410,1);CHECK(s5l8920_fmi_service_io(&fmi,NULL,&status_ops,&status_fixture));wr(0x444,1);
        if(omit!=0)wr(0x44c,0x4040);
        if(omit!=1)wr(0x440,0);
        if(omit!=2)wr(0x10,0);
        if(omit!=3)wr(0x424,0);
        reject(0x410,0x50);
    }
    init();status_init(0);wr(0x400,1);wr(0x408,0x11111);wr(0x414,0x70);reject(0x410,1);
    wr(0x40c,1);status_setup(&fmi);wr(0x40c,2);CHECK(!fmi.status_mode);reject(0x410,0x50);
}
static void test_data(void) {
    for(unsigned chip=0;chip<8;chip++) {
        init();uint8_t address=chip&1?0x20:0;configure(chip,address);wr(0x814,0);wr(0x410,9);
        s5l8920_fmi_t pending=fmi;
        for(unsigned n=0;n<10;n++)CHECK(rd(0x444)==0);
        CHECK(!memcmp(&pending,&fmi,sizeof pending) && !endpoint.calls);
        reject(0x40c,0);reject(0x414,0xff);reject(0x408,0);reject(0x410,0);reject(0x400,1);reject(0x814,0);
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
            reject(0x410,0x50);reject(0x408,1);reject(0x814,0);wr(0x410,0);reject_read(0x448);
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
    (void)board.bus.read32(&board,reg(0,0x814));
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED);s5l8920_clear_bus_failure(&board);
    cold=fmi;board.bus.write32(&board,gate(0),0);board.bus.write32(&board,reg(0,0x814),0);
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&cold,&fmi,sizeof cold));
    s5l8920_clear_bus_failure(&board);board.bus.write32(&board,gate(0),15);
    bw(0,0x814,0);s5l8920_clear_bus_failure(&board);bw(1,0x814,0);s5l8920_clear_bus_failure(&board);
    CHECK(board.bus.read32(&board,reg(0,0x814))==0 && !board.bus_failure.reason);s5l8920_clear_bus_failure(&board);
    CHECK(board.bus.read32(&board,reg(1,0x814))==0 && !board.bus_failure.reason);s5l8920_clear_bus_failure(&board);
    cold=fmi;board.bus.write32(&board,reg(0,0x814),1);
    CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED && !memcmp(&cold,&fmi,sizeof cold));s5l8920_clear_bus_failure(&board);
    board.bus.write16(&board,reg(0,0x814),0);
    CHECK(board.bus_failure.reason==S5L8920_BUS_ACCESS_UNIMPLEMENTED && !memcmp(&cold,&fmi,sizeof cold));s5l8920_clear_bus_failure(&board);
    board.bus.write32(&board,reg(0,0x815),0);
    CHECK(board.bus_failure.reason==S5L8920_BUS_ACCESS_UNIMPLEMENTED && !memcmp(&cold,&fmi,sizeof cold));s5l8920_clear_bus_failure(&board);
    (void)board.bus.read32(&board,reg(0,0x810));
    CHECK(board.bus_failure.reason==S5L8920_BUS_UNMAPPED && !memcmp(&cold,&fmi,sizeof cold));s5l8920_clear_bus_failure(&board);
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
    s5l8920_clear_bus_failure(&board);board.bus.write32(&board,reg(0,0x440),0x20);
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
static void test_board_pages(void) {
    const s5l8920_fmi_endpoint_t capability={NULL,read_page};
    CHECK(!s5l8920_fmi_board_service_endpoint(NULL,0,&capability,NULL));
    for(unsigned b=0;b<2;b++) {
        init();init_pages();CHECK(s5l8920_init(&board));if(!board.ram)return;
        CHECK(!s5l8920_fmi_board_service_endpoint(&board,b,&capability,&page_device));
        for(unsigned n=0;n<2;n++) {
            CHECK(s5l8920_clock_gate_configure(&board,S5L8920_FMI_GATE+n,15));
            CHECK(s5l8920_fmi_attach(&board,n,n?&other:&fmi));
        }
        s5l8920_fmi_t *f=b?&other:&fmi,*peer=b?&fmi:&other;
        page_device.controller=f;page_device.board=&board;page_device.reenter=true;
        page_configure(f,7,0x10000);bw(b,0x410,0xb);s5l8920_fmi_t before=*f,peer_before=*peer;
        board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;board.timebase_ticks=123;
        arm_cpu_t cpu=board.cpu;board.ram[0x100]=0x69;
        board.bus.write32(&board,gate(b),0);
        CHECK(s5l8920_fmi_board_service_endpoint(&board,b,&capability,&page_device));
        CHECK(!page_attempts && !memcmp(&before,f,sizeof before));
        (void)board.bus.read32(&board,reg(b,0x444));
        CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED);
        CHECK(!s5l8920_fmi_board_service_endpoint(&board,b,&capability,&page_device));
        s5l8920_clear_bus_failure(&board);board.bus.write32(&board,gate(b),15);
        CHECK(!s5l8920_fmi_board_service(&board,b,device,&endpoint) && !endpoint.calls);
        CHECK(s5l8920_fmi_board_service_endpoint(&board,b,&capability,&page_device));
        CHECK(page_device.reads==1 && page_device.chip==7 && page_device.row==0x10000);
        CHECK(f->commands==1 && f->status==0xb && !f->pending && !f->data_valid);
        CHECK(!memcmp(&peer_before,peer,sizeof peer_before) && !board.bus_failure.reason);
        CHECK(!memcmp(&cpu,&board.cpu,sizeof cpu) && board.timebase_ticks==123 && board.ram[0x100]==0x69);
        bw(b,0x444,0xb);bw(b,0x410,0xb);board.bus.write32(&board,gate(b),0x8000000f);
        CHECK(f->reset_asserted && !f->pending && !f->status && f->commands==1);
        CHECK(s5l8920_fmi_board_service_endpoint(&board,b,&capability,&page_device));
        CHECK(page_attempts==1 && page_device.reads==1 && page_device.busy);
        CHECK(!s5l8920_fmi_board_service_endpoint(&board,2,&capability,&page_device));
    }
}
static void test_board_status(void) {
    const s5l8920_fmi_endpoint_t pages_cap={NULL,status_page};
    CHECK(!s5l8920_fmi_board_service_io(NULL,0,NULL,&status_ops,NULL));
    for(unsigned b=0;b<2;b++) {
        init();status_init(2);CHECK(s5l8920_init(&board));if(!board.ram)return;
        for(unsigned n=0;n<2;n++) {
            CHECK(s5l8920_clock_gate_configure(&board,S5L8920_FMI_GATE+n,15));
            CHECK(s5l8920_fmi_attach(&board,n,n?&other:&fmi));
        }
        s5l8920_fmi_t *f=b?&other:&fmi,*peer=b?&fmi:&other,peer_before=*peer;
        status_fixture.controller=f;status_fixture.board=&board;
        page_configure(f,2,0x10203);bw(b,0x410,0xb);
        CHECK(s5l8920_fmi_board_service_endpoint(&board,b,&pages_cap,&status_fixture));bw(b,0x444,0xb);
        status_setup(f);bw(b,0x410,0x50);s5l8920_fmi_t before=*f;
        board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;board.timebase_ticks=127;
        arm_cpu_t cpu=board.cpu;board.ram[0x100]=0x96;
        board.bus.write32(&board,gate(b),0);
        CHECK(s5l8920_fmi_board_service_io(&board,b,NULL,&status_ops,&status_fixture));
        CHECK(!memcmp(&before,f,sizeof before) && !status_fixture.samples);
        board.bus.write32(&board,reg(b,0x10),0);CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED);
        CHECK(!s5l8920_fmi_board_service_io(&board,b,NULL,&status_ops,&status_fixture));
        s5l8920_clear_bus_failure(&board);board.bus.write32(&board,gate(b),15);
        CHECK(!s5l8920_fmi_board_service_endpoint(&board,b,&pages_cap,&status_fixture));
        CHECK(s5l8920_fmi_board_service_io(&board,b,NULL,&status_ops,&status_fixture));
        CHECK(f->pending && f->data==0x80 && f->status==0 && f->status_samples==1);
        for(unsigned n=0;n<4;n++)status_progress();
        CHECK(s5l8920_fmi_board_service_io(&board,b,NULL,&status_ops,&status_fixture));
        CHECK(!f->pending && f->data==0xc0 && f->status==0x20 && f->status_samples==2);
        CHECK(!memcmp(&cpu,&board.cpu,sizeof cpu) && board.timebase_ticks==127 && board.ram[0x100]==0x96);
        CHECK(!memcmp(&peer_before,peer,sizeof peer_before) && !board.bus_failure.reason);
        bw(b,0x410,0);bw(b,0x444,0x20);bw(b,0x410,0x50);
        board.bus.write32(&board,gate(b),0x8000000f);
        CHECK(!f->pending && !f->status_mode && !f->page_issued && f->status_samples==2);
        CHECK(s5l8920_fmi_board_service_io(&board,b,NULL,&status_ops,&status_fixture));
        CHECK(status_fixture.mode && status_fixture.samples==2);
        CHECK(!s5l8920_fmi_board_service_io(&board,2,NULL,&status_ops,&status_fixture));
    }
}
int main(void) {
    test_configuration();test_data();test_unselected_bus();test_timing_before_mode();test_ecc_mask();test_page_address_staging();test_page_command_queue();test_page_commands();test_status_setup_queue();test_status_polling();test_status_guards();test_reset();test_board();test_board_pages();test_board_status();s5l8920_free(&board);
    printf("s5l8920_fmi: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
