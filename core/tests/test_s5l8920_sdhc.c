/* Reset domain semantics and bounded IOP access, without firmware or a card.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_sdhc.h"
#include "s5l8920_iop.h"
#include <stdio.h>
#include <string.h>
static unsigned passed,failed;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);}}while(0)
static const struct {uint32_t offset,value;unsigned size;} registers[]={
    {0x00,0x12345678,4},{0x04,0x7200,2},{0x06,7,2},{0x08,0x89abcdef,4},
    {0x0c,0x37,2},{0x28,3,1},{0x2e,14,1},
    {0x34,0x1ff,2},{0x36,0x1ff,2},{0x38,0x1ff,2},{0x3a,0x1ff,2}
};
static uint32_t read_reg(const s5l8920_sdhc_t *s,uint32_t off,unsigned size) {
    uint32_t v=0xdeadbeef;CHECK(s5l8920_sdhc_read(s,off,size,&v));return v;
}
static void program(s5l8920_sdhc_t *s) {
    *s=(s5l8920_sdhc_t){0};CHECK(s5l8920_sdhc_init(s));
    for(unsigned n=0;n<sizeof registers/sizeof registers[0];n++) {
        CHECK(s5l8920_sdhc_write(s,registers[n].offset,registers[n].size,registers[n].value));
        CHECK(read_reg(s,registers[n].offset,registers[n].size)==registers[n].value);
    }
}
static void test_reset_domains(void) {
    for(unsigned mask=0;mask<8;mask++) {
        s5l8920_sdhc_t s;program(&s);
        /* Inject events into the component state solely to exercise reset
         * semantics. No producer/card/command engine is claimed by this test. */
        s.normal_status=0x1ff;s.error_status=0x1ff;
        s5l8920_sdhc_t before=s;
        CHECK(s5l8920_sdhc_init(&s) && !memcmp(&s,&before,sizeof s));
        CHECK(s5l8920_sdhc_write(&s,0x2f,1,mask));
        before=s;
        for(unsigned n=0;n<4;n++) {
            CHECK(read_reg(&s,0x2f,1)==mask);
            CHECK(s5l8920_sdhc_service(&s,false));
            CHECK(!memcmp(&s,&before,sizeof s));
        }
        CHECK(s5l8920_sdhc_write(&s,0x2f,1,0));
        CHECK(s5l8920_sdhc_write(&s,0x2f,1,mask));
        CHECK(!memcmp(&s,&before,sizeof s));
        if(mask) {
            CHECK(!s5l8920_sdhc_write(&s,8,4,0));
            CHECK(!s5l8920_sdhc_write(&s,0x2f,1,mask==7?1:7));
            CHECK(!memcmp(&s,&before,sizeof s));
        }
        CHECK(s5l8920_sdhc_service(&s,true) && !s.reset_pending);
        if(mask&1u) {
            CHECK(s.initialized);
            for(unsigned n=0;n<sizeof registers/sizeof registers[0];n++)
                CHECK(read_reg(&s,registers[n].offset,registers[n].size)==0);
            CHECK(read_reg(&s,0x30,2)==0 && read_reg(&s,0x32,2)==0);
        }else {
            unsigned expected=0x1ff;
            if(mask&2u)expected&=~1u;
            if(mask&4u)expected&=~0x3eu;
            CHECK(read_reg(&s,0x30,2)==(0x8000u|expected));
            CHECK(read_reg(&s,0x32,2)==0x1ff);
            for(unsigned n=0;n<sizeof registers/sizeof registers[0];n++)
                CHECK(read_reg(&s,registers[n].offset,registers[n].size)==registers[n].value);
        }
        before=s;CHECK(s5l8920_sdhc_service(&s,true) && !memcmp(&s,&before,sizeof s));
    }
    s5l8920_sdhc_t s={0};CHECK(s5l8920_sdhc_init(&s));
    s.normal_status=0x1ff;s.error_status=0x1ff;
    CHECK(s5l8920_sdhc_write(&s,0x30,2,0x8001));
    CHECK(read_reg(&s,0x30,2)==0x81fe && read_reg(&s,0x32,2)==0x1ff);
    CHECK(s5l8920_sdhc_write(&s,0x32,2,0x1ff));
    CHECK(read_reg(&s,0x30,2)==0x1fe);
}
static void test_register_refusals(void) {
    s5l8920_sdhc_t s;program(&s);
    for(unsigned off=0;off<0x1004;off++)for(unsigned size=0;size<=8;size++) {
        bool valid=false;
        for(unsigned n=0;n<sizeof registers/sizeof registers[0];n++)
            if(off==registers[n].offset && size==registers[n].size)valid=true;
        if((off==0x30 || off==0x32) && size==2)valid=true;
        if(off==0x2f && size==1)valid=true;
        if(valid)continue;
        s5l8920_sdhc_t before=s;uint32_t out=0xdeadbeef;
        CHECK(!s5l8920_sdhc_read(&s,off,size,&out) && out==0xdeadbeef);
        CHECK(!s5l8920_sdhc_write(&s,off,size,0) && !memcmp(&s,&before,sizeof s));
    }
    const struct {unsigned off,size;uint32_t value;} bad[]={
        {4,2,0x8000},{6,2,0x10000},{0xc,2,8},{0x28,1,4},{0x28,1,0x100},
        {0x2e,1,15},{0x2f,1,8},{0x2f,1,0x100},{0x30,2,0x200},
        {0x32,2,0x200},{0x34,2,0x8000},{0x36,2,0x200},{0x38,2,0x8000},{0x3a,2,0x200}
    };
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];n++) {
        s5l8920_sdhc_t before=s;
        CHECK(!s5l8920_sdhc_write(&s,bad[n].off,bad[n].size,bad[n].value));
        CHECK(!memcmp(&s,&before,sizeof s));
    }
    CHECK(!s5l8920_sdhc_init(NULL) && !s5l8920_sdhc_service(NULL,true));
    CHECK(!s5l8920_sdhc_read(&s,0,4,NULL));
    s=(s5l8920_sdhc_t){0};uint32_t out=7;
    CHECK(!s5l8920_sdhc_read(&s,0x2f,1,&out) && out==7);
    CHECK(!s5l8920_sdhc_write(&s,0x2f,1,1) && !s5l8920_sdhc_service(&s,true));
}

static s5l8920_iop_t iop;
static s5l8920_sdhc_t device;
static uint8_t ram[0x1000];
static unsigned calls;
static bool available;
static uint8_t *memory(void *owner,uint32_t address,uint32_t size) {
    CHECK(owner==&device);
    if(address<0x41000000u || size>sizeof ram || address-0x41000000u>sizeof ram-size)return NULL;
    return ram+address-0x41000000u;
}
static bool access_sdhc(void *owner,uint32_t offset,unsigned size,bool write,uint32_t *value) {
    CHECK(owner==&device && iop.servicing && iop.sdhc==access_sdhc);
    calls++;s5l8920_iop_t before=iop;
    CHECK(s5l8920_iop_service_with_sdhc(&iop,memory,NULL,NULL,NULL,access_sdhc,owner)==ARM_HALT);
    CHECK(!s5l8920_iop_reset_line(&iop,true) && !memcmp(&iop,&before,sizeof iop));
    if(!available)return false;
    return write?s5l8920_sdhc_write(&device,offset,size,*value):s5l8920_sdhc_read(&device,offset,size,value);
}
static void bindings_cleared(void) {
    CHECK(!iop.servicing && !iop.memory && !iop.signal_ap && !iop.timebase &&
          !iop.fmi && !iop.sdhc && !iop.memory_owner);
}
static void setup(uint32_t instruction,uint32_t address,uint32_t value) {
    memset(ram,0,sizeof ram);device=(s5l8920_sdhc_t){0};
    CHECK(s5l8920_sdhc_init(&device));s5l8920_iop_init(&iop);
    CHECK(s5l8920_iop_reset_line(&iop,true));CHECK(s5l8920_iop_reset_line(&iop,false));
    CHECK(s5l8920_iop_write(&iop,0x110,0x41000000u,memory,&device));
    CHECK(s5l8920_iop_write(&iop,0x114,0x3ffff000u,memory,&device));
    CHECK(s5l8920_iop_write(&iop,0x118,S5L8920_IOP_BASE,memory,&device));
    CHECK(s5l8920_iop_write(&iop,0x100,1,memory,&device));
    for(unsigned n=0;n<4;n++)ram[n]=(uint8_t)(instruction>>(8*n));
    iop.cpu.r[0]=address;iop.cpu.r[1]=value;calls=0;available=true;
}
static arm_status_t step(void) {
    arm_status_t status=s5l8920_iop_service_with_sdhc(&iop,memory,NULL,NULL,NULL,access_sdhc,&device);
    bindings_cleared();return status;
}
static void test_iop_access(void) {
    const struct {uint32_t store,load,offset,value;unsigned size;} ops[]={
        {0xe5801000,0xe5901000,0,0x12345678,4},
        {0xe1c010b0,0xe1d010b0,6,19,2},
        {0xe5c01000,0xe5d01000,0x2f,2,1}
    };
    for(unsigned n=0;n<3;n++) {
        setup(ops[n].store,S5L8920_SDHC_BASE+ops[n].offset,ops[n].value);
        CHECK(step()==ARM_OK && iop.cpu.cycles==1 && calls==1 && !iop.bytes_written);
        CHECK(read_reg(&device,ops[n].offset,ops[n].size)==ops[n].value);
        for(unsigned k=0;k<4;k++)ram[4+k]=(uint8_t)(ops[n].load>>(8*k));
        iop.cpu.r[1]=0xdeadbeef;s5l8920_sdhc_t before=device;
        CHECK(step()==ARM_OK && iop.cpu.r[1]==ops[n].value && calls==2);
        CHECK(!memcmp(&device,&before,sizeof device) && !iop.bytes_written);
    }
    /* Bus refusal keeps architectural registers/retirement and device state. */
    for(unsigned mode=0;mode<5;mode++) {
        uint32_t address=mode==2?S5L8920_SDHC_BASE+0x1000:S5L8920_SDHC_BASE+(mode==3?0x40:0x2f);
        setup(mode==4?0xe5c01000u:0xe5d01000u,address,mode==4?8:0xdeadbeef);
        available=mode!=1;arm_cpu_t cpu=iop.cpu;s5l8920_sdhc_t before=device;
        arm_status_t status=mode==0?s5l8920_iop_service(&iop,memory,&device):step();
        CHECK(status==ARM_HALT && iop.cpu.cycles==cpu.cycles && !memcmp(cpu.r,iop.cpu.r,sizeof cpu.r));
        CHECK(iop.cpu.cpsr==cpu.cpsr && !memcmp(&device,&before,sizeof device));
        CHECK(iop.failure.address==address && iop.failure.size==1 && iop.failure.write==(mode==4));
        CHECK(iop.failure.reason==((mode==0 || mode==2)?S5L8920_IOP_UNMAPPED:S5L8920_IOP_REGISTER_REFUSED));
        CHECK(calls==((mode==0 || mode==2)?0u:1u));bindings_cleared();
        s5l8920_iop_t stopped=iop;CHECK(step()==ARM_HALT && !memcmp(&iop,&stopped,sizeof iop));
    }
    setup(0xee100f10,S5L8920_SDHC_BASE,0); /* unsupported CP15 */
    CHECK(step()==ARM_UNDEFINED && !calls && !iop.failure.reason);
    setup(0xe5c01000,S5L8920_SDHC_BASE+0x2f,1);CHECK(step()==ARM_OK);
    s5l8920_sdhc_t pending=device;
    CHECK(s5l8920_iop_write(&iop,0x100,0,memory,&device));
    CHECK(step()==ARM_OK && calls==1 && !memcmp(&device,&pending,sizeof device));
    CHECK(s5l8920_iop_reset_line(&iop,true));
    CHECK(step()==ARM_OK && calls==1 && !memcmp(&device,&pending,sizeof device));
    CHECK(s5l8920_sdhc_service(&device,true) && !device.reset_pending);
}
static unsigned raw_size;
static bool raw_write;
static uint32_t raw_address;
static uint8_t *raw_bus(void *owner,uint32_t address,uint32_t size) {
    if(raw_write) {
        if(raw_size==2)iop.bus.write16(&iop,raw_address,1);
        else iop.bus.write32(&iop,raw_address,1);
    }else {
        if(raw_size==2)(void)iop.bus.read16(&iop,raw_address);
        else (void)iop.bus.read32(&iop,raw_address);
    }
    return memory(owner,address,size);
}
static void test_bus_shape(void) {
    for(unsigned n=0;n<8;n++) {
        setup(0xe1a00000,0,0);raw_size=(n&2)?4:2;raw_write=(n&1)!=0;
        raw_address=S5L8920_SDHC_BASE+((n&4)?0xfff:1);
        CHECK(s5l8920_iop_service_with_sdhc(&iop,raw_bus,NULL,NULL,NULL,access_sdhc,&device)==ARM_HALT);
        CHECK(!calls && iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED);
        CHECK(iop.failure.size==raw_size && iop.failure.write==raw_write && !iop.cpu.cycles);
        bindings_cleared();
    }
}
int main(void) {
    test_reset_domains();test_register_refusals();test_iop_access();test_bus_shape();
    printf("s5l8920_sdhc: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
