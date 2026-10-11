/* Board-level DMA/AES data, bounds, scheduling and refusal regressions.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define FEED (S5L8920_CDMA_BASE+0x1000u)
#define DRAIN (S5L8920_CDMA_BASE+0x2000u)
#define AES S5L8920_AES_CONTEXT1
#define SRC 0x41000000u
#define DST 0x41001000u
#define DESC 0x41002000u
#define GATE (S5L8920_CLOCK_GATE_BASE+4u*S5L8920_CDMA_GATE)
static s5l8920_t board;
static s5l8920_cdma_t cdma;
static unsigned passed,failed;
#define CHECK(x) do { if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);} } while(0)
/* NIST SP800-38A F.2.1/F.2.3/F.2.5. All 4 blocks, independent of aes.c. */
static const char *keys[]={
    "2b7e151628aed2a6abf7158809cf4f3c",
    "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
    "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"};
static const char *ciphertexts[]={
    "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2"
    "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7",
    "4f021db243bc633d7178183a9fa071e8b4d9ada9ad7dedf4e5e738763f69145a"
    "571b242012fb7ae07fa9baac3df102e008b0e27988598881d920a9e64f5615cd",
    "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d"
    "39f23369a9d9bacfa530e26304231461b2eb05e2c39be9fcda6c19078c6a9d1b"};
static const char *plaintext="6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
    "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710";
static unsigned nibble(char x) { return x<='9'?(unsigned)(x-'0'):(unsigned)(x-'a'+10); }
static void decode(uint8_t *p,const char *hex) {
    while(*hex) { *p++=(uint8_t)((nibble(hex[0])<<4)|nibble(hex[1]));hex+=2; }
}
static uint8_t *at(uint32_t address) { return board.ram+address-S5L8920_RAM_BASE; }
static void wr(uint32_t address,uint32_t value) { board.bus.write32(&board,address,value); }
static uint32_t rd(uint32_t address) { return board.bus.read32(&board,address); }
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void init(unsigned gate) {
    s5l8920_free(&board);CHECK(s5l8920_init(&board));s5l8920_cdma_init(&cdma);
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_CDMA_GATE,gate));
    CHECK(s5l8920_cdma_attach(&board,&cdma));
}
static void context(unsigned size,bool decrypt) {
    uint8_t key[32]={0};decode(key,keys[size]);
    for(unsigned i=0;i<4u;++i)wr(AES+0x10u+4u*i,0x03020100u+0x04040404u*i);
    for(unsigned i=0;i<4u+2u*size;++i)wr(AES+0x20u+4u*i,word(key+4u*i));
    wr(AES,0x120100u|(size<<18)|(decrypt?0u:0x10000u));
}
static void descriptors(uint32_t source,uint32_t dest,unsigned length) {
    memset(at(DESC),0,128u);
    wr(DESC,DESC+32u);wr(DESC+4u,0x30103u);wr(DESC+8u,source);wr(DESC+12u,length);
    wr(DESC+64u,DESC+96u);wr(DESC+68u,0x103u);wr(DESC+72u,dest);wr(DESC+76u,length);
    wr(FEED,2u);wr(DRAIN,2u);
    wr(FEED+12u,length);wr(FEED+20u,DESC);wr(FEED,0x1c0180u);
    wr(DRAIN+12u,length);wr(DRAIN+20u,DESC+64u);wr(DRAIN,0x1c0080u);
}
static void prepare(void) {
    init(15u);context(0u,false);descriptors(SRC,DST,64u);
    decode(at(SRC),plaintext);memset(at(DST),0xa5,80u);CHECK(!board.bus_failure.reason);
}
static void refuse(uint32_t address,unsigned bytes,uint32_t value,s5l8920_bus_reason_t reason) {
    s5l8920_cdma_t before=cdma;uint8_t output[80];memcpy(output,at(DST),sizeof output);
    if(bytes==1u)board.bus.write8(&board,address,(uint8_t)value);
    else if(bytes==2u)board.bus.write16(&board,address,(uint16_t)value);
    else wr(address,value);
    CHECK(board.bus_failure.reason==reason && board.bus_failure.address==address);
    CHECK(!memcmp(&cdma,&before,sizeof cdma) && !memcmp(output,at(DST),sizeof output));
    CHECK(!s5l8920_cdma_board_service(&board));
    CHECK(!memcmp(&cdma,&before,sizeof cdma));
    s5l8920_clear_bus_failure(&board);
}
static void vectors(void) {
    init(15u);
    for(unsigned size=0;size<3u;++size)for(unsigned decrypt=0;decrypt<2u;++decrypt)
    for(unsigned inplace=0;inplace<2u;++inplace)for(unsigned order=0;order<2u;++order) {
        s5l8920_cdma_init(&cdma);uint32_t dest=inplace?SRC:DST;
        context(size,decrypt!=0u);descriptors(SRC,dest,64u);
        uint8_t expected[64],input[64],guard[16];memset(guard,0xa5,sizeof guard);
        decode(input,decrypt?ciphertexts[size]:plaintext);decode(expected,decrypt?plaintext:ciphertexts[size]);
        memcpy(at(SRC),input,64u);memset(at(dest+64u),0xa5,16u);
        if(!inplace)memset(at(dest),0xa5,64u);
        uint8_t untouched[80];memcpy(untouched,at(dest),80u);
        wr(order?DRAIN:FEED,order?0x81u:0x181u);
        CHECK(!board.bus_failure.reason && (rd(order?DRAIN:FEED)&0x30000u)==0x10000u);
        CHECK(s5l8920_cdma_board_service(&board) && !memcmp(untouched,at(dest),80u));
        wr(order?FEED:DRAIN,order?0x181u:0x81u);
        CHECK(!board.bus_failure.reason && !(rd(DRAIN)&0x80000u));
        for(unsigned i=0;i<16u;++i) { (void)rd(FEED);(void)rd(DRAIN); }
        CHECK(!memcmp(untouched,at(dest),80u) && rd(FEED+12u)==64u);
        for(unsigned block=0;block<4u;++block) {
            board.cpu.excl_valid=true;board.cpu.a8_excl_size=4u;
            arm_cpu_t cpu=board.cpu;uint64_t ticks=board.timebase_ticks;
            CHECK(s5l8920_cdma_board_service(&board));
            cpu.excl_valid=false;cpu.a8_excl_size=0;
            CHECK(!memcmp(&cpu,&board.cpu,sizeof cpu) && ticks==board.timebase_ticks);
            unsigned processed=16u*(block+1u);
            CHECK(!memcmp(at(dest),expected,processed));
            CHECK(!memcmp(at(dest)+processed,untouched+processed,80u-processed));
            CHECK(rd(FEED+12u)==64u-processed && rd(DRAIN+12u)==64u-processed);
            CHECK(rd(FEED+16u)==SRC+processed && rd(DRAIN+16u)==dest+processed);
            CHECK(((rd(FEED)&0x30000u)!=0u)==(block<3u));
            CHECK(((rd(DRAIN)&0x80000u)!=0u)==(block==3u));
        }
        CHECK(rd(FEED+20u)==DESC+32u && rd(DRAIN+20u)==DESC+96u);
        CHECK(!memcmp(at(dest+64u),guard,16u));
        if(!inplace)CHECK(!memcmp(at(SRC),input,64u));
        s5l8920_cdma_t completed=cdma;
        CHECK(s5l8920_cdma_board_service(&board) && !memcmp(&completed,&cdma,sizeof cdma));
        CHECK((rd(DRAIN)&0x80000u)!=0u);wr(DRAIN,0x80080u);
        CHECK(rd(DRAIN)==0x80u);
        CHECK(!board.bus_failure.reason);
    }
}
static void guards(void) {
    prepare();
    refuse(AES,4u,0x230100u,S5L8920_BUS_REGISTER_REFUSED); /* GID */
    refuse(AES,4u,0x430100u,S5L8920_BUS_REGISTER_REFUSED); /* UID */
    refuse(AES,4u,0x1f0100u,S5L8920_BUS_REGISTER_REFUSED); /* Reserved key size */
    refuse(AES,4u,0x110100u,S5L8920_BUS_REGISTER_REFUSED); /* Disabled filter */
    refuse(AES,4u,0x130200u,S5L8920_BUS_REGISTER_REFUSED); /* Other channel */
    refuse(AES+0x10u,1u,0u,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(AES+0x10u,2u,0u,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(AES+0x11u,4u,0u,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(FEED,1u,2u,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(FEED+1u,4u,2u,S5L8920_BUS_ACCESS_UNIMPLEMENTED);
    refuse(FEED+4u,4u,0u,S5L8920_BUS_REGISTER_REFUSED); /* Peripheral settings */
    refuse(FEED+8u,4u,0u,S5L8920_BUS_REGISTER_REFUSED); /* Peripheral FIFO */
    refuse(FEED,4u,0x18u,S5L8920_BUS_REGISTER_REFUSED);
    refuse(DRAIN,4u,0x89u,S5L8920_BUS_REGISTER_REFUSED); /* Interrupt mode */
    refuse(FEED,4u,0x1a1u,S5L8920_BUS_REGISTER_REFUSED); /* Hold */
    refuse(FEED,4u,0x184u,S5L8920_BUS_REGISTER_REFUSED); /* Abort */
    refuse(FEED,4u,0x81u,S5L8920_BUS_REGISTER_REFUSED); /* Wrong context */
    refuse(S5L8920_CDMA_BASE+0x3000u,4u,2u,S5L8920_BUS_UNMAPPED);
    refuse(AES+0x1000u,4u,0u,S5L8920_BUS_UNMAPPED);
    (void)rd(AES+0x20u);CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED);
    s5l8920_clear_bus_failure(&board);
    for(unsigned i=0;i<4u;++i) {
        uint8_t old=cdma.iv_programmed;cdma.iv_programmed&=(uint8_t)~(1u<<i);
        refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);cdma.iv_programmed=old;
    }
    for(unsigned i=0;i<4u;++i) {
        uint8_t old=cdma.key_programmed;cdma.key_programmed&=(uint8_t)~(1u<<i);
        refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);cdma.key_programmed=old;
    }
    const uint32_t bad_addresses[]={0u,0x87001000u,0x4ffffff0u,0xfffffff0u};
    for(unsigned i=0;i<4u;++i) {
        wr(DESC+8u,bad_addresses[i]);refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);
    }
    wr(DESC+8u,SRC);
    for(unsigned i=0;i<3u;++i) {
        wr(DESC,i==0u?DESC:(i==1u?DESC+1u:0xffffffe0u));
        refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);
    }
    wr(DESC,DESC+32u);wr(DESC+36u,0x103u);
    refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);wr(DESC+36u,0u);
    wr(DESC+4u,0x30102u);refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);wr(DESC+4u,0x30103u);
    wr(DESC+12u,48u);refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);wr(DESC+12u,64u);
    wr(FEED+12u,63u);refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);wr(FEED+12u,64u);
    wr(FEED+20u,DESC+4u);refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);wr(FEED+20u,DESC);
    wr(FEED,0x181u);wr(DESC+72u,SRC+16u);
    refuse(DRAIN,4u,0x81u,S5L8920_BUS_REGISTER_REFUSED);wr(DESC+72u,DST);
    refuse(AES+0x20u,4u,0u,S5L8920_BUS_REGISTER_REFUSED);
    refuse(FEED,4u,2u,S5L8920_BUS_REGISTER_REFUSED);
    refuse(DRAIN,4u,2u,S5L8920_BUS_REGISTER_REFUSED);
    wr(DRAIN,0x81u);CHECK(!board.bus_failure.reason);
    wr(GATE,0u);s5l8920_cdma_t before=cdma;
    CHECK(s5l8920_cdma_board_service(&board) && !memcmp(&before,&cdma,sizeof cdma));
    refuse(AES+0x20u,4u,0u,S5L8920_BUS_REGISTER_REFUSED);
    wr(GATE,15u);CHECK(s5l8920_cdma_board_service(&board) && cdma.channel[0].remaining==48u);
    uint8_t output[80];memcpy(output,at(DST),80u);
    CHECK(s5l8920_reset(&board) && !board.cdma && !s5l8920_cdma_board_service(&board));
    CHECK(!memcmp(output,at(DST),80u));s5l8920_cdma_init(&cdma);
    CHECK(s5l8920_cdma_attach(&board,&cdma));
    (void)rd(FEED);CHECK(board.bus_failure.reason==S5L8920_BUS_REGISTER_REFUSED);
    s5l8920_clear_bus_failure(&board);
}
static void aliases_and_irqs(void) {
    prepare();CHECK(s5l8920_set_ram_boot_window(&board,true));
    wr(DESC+8u,SRC-S5L8920_RAM_BASE);wr(DESC+72u,SRC); /* Same backing, in-place. */
    wr(FEED+20u,DESC-S5L8920_RAM_BASE);wr(FEED,0x181u);wr(DRAIN,0x81u);
    CHECK(!board.bus_failure.reason);
    CHECK(s5l8920_set_ram_boot_window(&board,false));s5l8920_cdma_t before=cdma;
    CHECK(!s5l8920_cdma_board_service(&board) && !memcmp(&cdma,&before,sizeof cdma));
    CHECK(s5l8920_set_ram_boot_window(&board,true));
    unsigned source=44u;
    uint32_t vic=S5L8920_VIC_BASE+S5L8920_VIC_STRIDE;
    wr(vic+0x10u,1u<<(source%32u));CHECK(!board.cpu.irq_line);
    for(unsigned i=0;i<4u;++i)CHECK(s5l8920_cdma_board_service(&board));
    CHECK(!board.cpu.irq_line && !(rd(vic+8u)&(1u<<(source%32u))));
    uint8_t expected[64];decode(expected,ciphertexts[0]);CHECK(!memcmp(at(SRC),expected,64u));
    wr(DRAIN,0x80080u);CHECK(!board.cpu.irq_line);
    CHECK(s5l8920_set_irq(&board,source,true));CHECK(board.cpu.irq_line);
    wr(DRAIN,0x80080u);CHECK(board.cpu.irq_line);
    CHECK(s5l8920_set_irq(&board,source,false));CHECK(!board.cpu.irq_line);
}
static void lifetime_and_gates(void) {
    s5l8920_cdma_t other={0};s5l8920_free(&board);CHECK(s5l8920_init(&board));
    CHECK(!s5l8920_cdma_attach(&board,&other));s5l8920_cdma_init(&other);
    CHECK(!s5l8920_cdma_attach(&board,&other));
    for(unsigned gate=0;gate<15u;++gate) {
        init(gate);s5l8920_cdma_t before=cdma;
        CHECK(s5l8920_cdma_board_service(&board)==(gate==0u));
        CHECK(!memcmp(&before,&cdma,sizeof cdma));
        refuse(FEED,4u,2u,S5L8920_BUS_REGISTER_REFUSED);
        CHECK(s5l8920_cdma_attach(&board,&cdma));
        CHECK(!s5l8920_cdma_attach(&board,&other));
    }
    init(15u);uint32_t value=0x12345678u;
    CHECK(!s5l8920_cdma_read(&cdma,false,0x1000u,&value) && value==0x12345678u);
    refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);
    context(0u,false);descriptors(SRC,DST,0u);
    refuse(FEED,4u,0x181u,S5L8920_BUS_REGISTER_REFUSED);
    descriptors(SRC,DST,64u);wr(DRAIN+12u,48u);wr(DESC+76u,48u);
    wr(FEED,0x181u);refuse(DRAIN,4u,0x81u,S5L8920_BUS_REGISTER_REFUSED);
    wr(DRAIN+12u,64u);wr(DESC+76u,64u);wr(DESC+72u,0xfffffff0u);
    refuse(DRAIN,4u,0x81u,S5L8920_BUS_REGISTER_REFUSED);
    wr(DESC+72u,DST);wr(DRAIN,0x81u);CHECK(!board.bus_failure.reason);
    CHECK(s5l8920_cdma_board_service(&board));
    uint8_t output[64];memcpy(output,at(DST),64u);
    refuse(FEED+12u,4u,0u,S5L8920_BUS_REGISTER_REFUSED);
    refuse(DRAIN+20u,4u,DESC,S5L8920_BUS_REGISTER_REFUSED);
    CHECK(!memcmp(output,at(DST),64u));
    /* Exact RAM upper edge is valid, one extra block is not. */
    for(unsigned direction=0;direction<2u;++direction) {
        prepare();uint32_t edge=S5L8920_RAM_BASE+S5L8920_RAM_SIZE-64u;
        uint32_t source=direction?SRC:edge,dest=direction?edge:DST;
        decode(at(source),plaintext);descriptors(source,dest,64u);
        wr(FEED,0x181u);wr(DRAIN,0x81u);
        for(unsigned i=0;i<4u;++i)CHECK(s5l8920_cdma_board_service(&board));
        uint8_t expected[64];decode(expected,ciphertexts[0]);CHECK(!memcmp(at(dest),expected,64u));
        CHECK(!board.bus_failure.reason);
    }
}
int main(void) {
    vectors();guards();aliases_and_irqs();lifetime_and_gates();s5l8920_free(&board);
    CHECK(!s5l8920_cdma_board_service(&board));
    CHECK(!s5l8920_cdma_attach(&board,&cdma));
    printf("CDMA AES: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
