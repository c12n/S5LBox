/* Explicit, partial ARMv4T profile: architectural boundaries and memory effects.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "arm.h"
#include "jit.h"
#include <stdio.h>
#include <string.h>

#define PROFILE ARM_ARCH_V4T
static arm_cpu_t c;
static uint8_t ram[4096];
static unsigned passed, failed, reads, writes, read_size, write_size;
static uint32_t last_read,last_write;
static bool fault;
#define CHECK(x) do { if (x) passed++; else { failed++; \
    printf("FAIL %s:%d: %s\n", __func__, __LINE__, #x); } } while (0)
static bool access_failed(void *ctx) { (void)ctx; return fault; }
static uint32_t read_width(uint32_t a, unsigned width) {
    reads++;read_size=width;last_read=a;
    if (a > sizeof ram - width) { fault = true; return 0; }
    uint32_t v = 0;
    for (unsigned i = 0; i < width; ++i) v |= (uint32_t)ram[a+i] << (8u*i);
    return v;
}
static void put(uint32_t a, uint32_t v, unsigned width) {
    if (a > sizeof ram - width) { fault = true; return; }
    for (unsigned i = 0; i < width; ++i) ram[a+i] = (uint8_t)(v >> (8u*i));
}
static uint32_t r32(void *ctx, uint32_t a) { (void)ctx; return read_width(a, 4); }
static uint16_t r16(void *ctx, uint32_t a) { (void)ctx; return (uint16_t)read_width(a, 2); }
static uint8_t r8(void *ctx, uint32_t a) { (void)ctx; return (uint8_t)read_width(a, 1); }
static void w32(void *ctx, uint32_t a, uint32_t v) { (void)ctx; writes++;write_size=4;last_write=a;put(a,v,4); }
static void w16(void *ctx, uint32_t a, uint16_t v) { (void)ctx; writes++;write_size=2;last_write=a;put(a,v,2); }
static void w8(void *ctx, uint32_t a, uint8_t v) { (void)ctx; writes++;write_size=1;last_write=a;put(a,v,1); }
static arm_bus_t bus = { .read32=r32, .read16=r16, .read8=r8,
    .write32=w32, .write16=w16, .write8=w8, .access_failed=access_failed };
static void init(uint32_t insn) {
    memset(ram, 0, sizeof ram); fault=false; reads=writes=0;
    read_size=write_size=0;last_read=last_write=0;
    CHECK(arm_reset_profile(&c, &bus, PROFILE)); put(0, insn, 4);
}
static void test_reset(void) {
    memset(&c, 0xa5, sizeof c);
    CHECK(arm_reset_profile(&c, &bus, PROFILE));
    CHECK(c.arch == PROFILE && c.cpsr == (ARM_MODE_SVC|ARM_CPSR_I|ARM_CPSR_F));
    CHECK(c.r[15] == 0 && c.cycles == 0 && c.cp15.sctlr == 0 && !c.excl_valid);
    CHECK(!arm_arch_has_movw_movt(PROFILE) && !arm_arch_has_a32_divide(PROFILE));
    CHECK(!arm_arch_has_a32_barriers(PROFILE) && !arm_arch_has_a32_bitfield(PROFILE));
    CHECK(!arm_arch_uses_thumb2_encoding(PROFILE));
    arm_bus_t plain_bus=bus; plain_bus.access_failed=NULL; c.bus=&plain_bus;
    CHECK(!jit_cpu_supported(&c));
    arm_reset(&c, &bus);
    CHECK(c.arch == ARM_ARCH_V6_ARM1176 && (c.cpsr & ARM_CPSR_A));
}
static void test_bootstrap(void) {
    /* Authored loop zeroes a region, returns through BX, and loads actual RAM. */
    static const uint32_t program[] = {
        0xe3a00080u, /* MOV r0,#128 */
        0xe3a01000u, /* MOV r1,#0 */
        0xe3a02004u, /* MOV r2,#4 */
        0xeb000003u, /* BL 0x20 */
        0xe5103010u, /* LDR r3,[r0,#-16] */
        0xea000004u, /* B 0x2c */
        0, 0,
        0xe4801004u, /* STR r1,[r0],#4 */
        0xe2522001u, /* SUBS r2,r2,#1 */
        0x1afffffcu, /* BNE 0x20 */
        0xe12fff1eu  /* BX lr */
    };
    init(0);
    for (unsigned i=0; i<sizeof program/sizeof program[0]; ++i) put(4u*i,program[i],4);
    memset(ram+128,0xa5,16);
    for (unsigned i=0; i<18; ++i) CHECK(arm_step(&c) == ARM_OK);
    CHECK(c.r[15] == 20 && c.r[0] == 144 && c.r[2] == 0 && c.r[3] == 0 && writes == 4);
    for (unsigned i=128; i<144; ++i) CHECK(ram[i] == 0);
}
static void test_pc_loads(void) {
    const arm_arch_t profiles[] = {PROFILE,ARM_ARCH_V6_ARM1176,ARM_ARCH_V7_CORTEX_A8};
    for (unsigned p=0; p<3; ++p) for (unsigned low=0; low<4; ++low) {
        init(0xe590f000u); /* LDR pc,[r0] */
        CHECK(arm_reset_profile(&c,&bus,profiles[p])); c.r[0]=128; put(128,256+low,4);
        arm_status_t expected = p && low==2 ? ARM_UNDEFINED : ARM_OK;
        CHECK(arm_step(&c)==expected);
        if (expected==ARM_OK) {
            CHECK(c.r[15]==(p && (low&1u) ? (256u+low)&~1u : 256u));
            CHECK(!!(c.cpsr&ARM_CPSR_T)==!!(p && (low&1u)));
        }
        init(0xe8b08002u); /* LDMIA r0!,{r1,pc} */
        CHECK(arm_reset_profile(&c,&bus,profiles[p])); c.r[0]=128;
        put(128,0x76543210u,4); put(132,256+low,4);
        CHECK(arm_step(&c)==expected);
        if (expected==ARM_OK) {
            CHECK(c.r[0]==136 && c.r[1]==0x76543210u);
            CHECK(c.r[15]==(p && (low&1u) ? (256u+low)&~1u : 256u));
            CHECK(!!(c.cpsr&ARM_CPSR_T)==!!(p && (low&1u)));
        }
    }
    init(0xe12fff10u); c.r[0]=257; /* BX is still an interworking instruction. */
    CHECK(arm_step(&c)==ARM_OK && c.r[15]==256 && (c.cpsr&ARM_CPSR_T));
    unsigned before_reads=reads;
    CHECK(arm_step(&c)==ARM_UNDEFINED && c.r[15]==256 && c.cycles==1 && reads==before_reads);
}
static void test_boundaries(void) {
    static const uint32_t refused[] = {
        0xe12fff30u, /* BLX reg */ 0xfa000000u, /* BLX imm */
        0xe16f0f11u, /* CLZ */ 0xe1020051u, /* QADD */
        0xe30b3eefu, /* MOVW */ 0xe34d3eadu, /* MOVT */
        0xe1910f9fu, /* LDREX */ 0xe1812f90u, /* STREX */
        0xe1c120d0u, /* LDRD */ 0xe6bf0f31u, /* REV */
        0xe7003211u, /* SMLAD */ 0xe713f011u, /* SDIV */
        0xf57ff01fu, /* CLREX */ 0xf57ff05fu, /* DMB */
        0xf10c0080u, /* CPS */ 0xf1010000u, /* SETEND */
        0xf5d0f000u, /* PLD */ 0xe320f003u, /* WFI */
        0xee100f10u, /* MRC CP15 */ 0xee010f10u, /* MCR CP15 */
        0xeeb00a40u, /* VFP */ 0xf2000840u, /* NEON */
        0xe1012090u, /* SWP */
        0xe1c120f0u, /* STRD */ 0xef000000u, /* SWI */
        0xe10ff000u, /* MRS pc */ 0xe128f00fu /* MSR flags,pc */
    };
    for (unsigned i=0; i<sizeof refused/sizeof refused[0]; ++i) {
        init(refused[i]); c.r[0]=128; c.r[1]=256; c.excl_valid=true;
        uint32_t regs[16], psr=c.cpsr; memcpy(regs,c.r,sizeof regs);
        CHECK(arm_step(&c)==ARM_UNDEFINED);
        CHECK(!memcmp(regs,c.r,sizeof regs) && c.cpsr==psr && c.cycles==0 && writes==0);
        CHECK(c.cp15.sctlr==0 && c.excl_valid);
    }
    /* Failed conditions must skip even unsupported encodings. */
    init(0x012fff30u); c.r[0]=128;
    CHECK(arm_step(&c)==ARM_OK && c.r[15]==4 && c.cycles==1);
    init(0xe3a0002au); c.cp15.sctlr=ARM_SCTLR_M;
    CHECK(arm_step(&c)==ARM_UNDEFINED && reads==0 && c.cycles==0);
    init(0xe3a0002au); c.cpsr|=ARM_CPSR_Q;
    CHECK(arm_step(&c)==ARM_UNDEFINED && reads==0 && c.cycles==0);
    init(0xe3a0002au); c.r[15]=2;
    CHECK(arm_step(&c)==ARM_UNDEFINED && reads==0 && c.cycles==0);
}
static void test_psr_and_irq(void) {
    init(0xe10f0000u); CHECK(arm_step(&c)==ARM_OK && c.r[0]==0xd3u);
    init(0xe121f000u); c.r[0]=0xd2u; c.r[13]=0x100;
    CHECK(arm_step(&c)==ARM_OK && c.cpsr==0xd2u && c.r[13]==0 && c.bank_r13[ARM_BANK_SVC]==0x100);
    init(0xe128f000u); c.r[0]=0xf0000000u;
    CHECK(arm_step(&c)==ARM_OK && c.cpsr==0xf00000d3u);
    const uint32_t bad[] = {0xe121f000u,0xe128f000u,0xe122f000u};
    const uint32_t values[] = {0xf3u,0x08000000u,0x100u};
    for (unsigned i=0;i<3;++i) {
        init(bad[i]); c.r[0]=values[i];
        CHECK(arm_step(&c)==ARM_UNDEFINED && c.cpsr==0xd3u && c.r[15]==0 && c.cycles==0);
    }
    init(0xe121f000u); arm_set_mode(&c,ARM_MODE_USR); c.r[0]=0xffffffffu;
    CHECK(arm_step(&c)==ARM_OK && c.cpsr==0xd0u); /* User control writes ignored. */
    init(0xe1b0f00eu); /* MOVS pc,lr: real IRQ entry and exception return */
    put(24,0xe25ef004u,4); c.cpsr=ARM_MODE_SVC; c.r[15]=128; c.r[13]=256;
    c.irq_line=true;
    CHECK(arm_step(&c)==ARM_OK && c.r[15]==24 && c.r[14]==132 && c.cpsr==0x92u);
    CHECK(c.spsr[ARM_BANK_IRQ]==ARM_MODE_SVC);
    c.irq_line=false;
    CHECK(arm_step(&c)==ARM_OK && c.r[15]==128 && c.cpsr==ARM_MODE_SVC && c.r[13]==256);
    init(0); c.cpsr=ARM_MODE_SVC|ARM_CPSR_T; c.r[15]=130; c.irq_line=c.fiq_line=true;
    CHECK(arm_step(&c)==ARM_OK && c.r[15]==28 && c.r[14]==134 && c.cpsr==0xd1u);
    CHECK(c.spsr[ARM_BANK_FIQ]==(ARM_MODE_SVC|ARM_CPSR_T));
    put(28,0xe25ef004u,4); c.irq_line=c.fiq_line=false;
    CHECK(arm_step(&c)==ARM_OK && c.r[15]==130 && c.cpsr==(ARM_MODE_SVC|ARM_CPSR_T));
    init(0xe168f000u); c.r[0]=ARM_CPSR_Q; /* Reserved SPSR bits cannot be enabled. */
    CHECK(arm_step(&c)==ARM_UNDEFINED && c.spsr[ARM_BANK_SVC]==0 && c.cycles==0);
    init(0xe1b0f00eu); c.r[14]=256; c.spsr[ARM_BANK_SVC]=ARM_MODE_SVC|ARM_CPSR_Q;
    CHECK(arm_step(&c)==ARM_UNDEFINED && c.cpsr==0xd3u && c.r[15]==0 && c.cycles==0);
}
static void test_multiply(void) {
    static const uint32_t values[]={0,1,2,0xffffffffu,0x80000000u,0x7fffffffu,0x13579bdfu};
    for(unsigned a=0;a<7;a++)for(unsigned b=0;b<7;b++)
    for(unsigned acc=0;acc<2;acc++)for(unsigned set=0;set<2;set++)for(unsigned cv=0;cv<4;cv++) {
        /* MUL{S} r2,r1,r3 or MLA{S} r2,r1,r3,r4. */
        init(0xe0020391u|(acc<<21)|(set<<20)|(acc?0x4000u:0));
        c.r[1]=values[a];c.r[3]=values[b];c.r[4]=0x12345678u;
        c.cpsr|=ARM_CPSR_N|ARM_CPSR_Z|(cv<<28);
        uint32_t before=c.cpsr;
        uint32_t result=(uint32_t)((uint64_t)values[a]*values[b]+(acc?0x12345678u:0));
        uint32_t flags=set?(before&~(ARM_CPSR_N|ARM_CPSR_Z))|
            (result&ARM_CPSR_N)|(result?0:ARM_CPSR_Z):before;
        CHECK(arm_step(&c)==ARM_OK && c.r[2]==result && c.r[15]==4 && c.cycles==1);
        CHECK(c.cpsr==flags && c.r[1]==values[a] && c.r[3]==values[b] && c.r[4]==0x12345678u);
        CHECK(reads==1 && writes==0);
    }
    const uint32_t aliases[]={0xe0030391u,0xe0222391u,0xe0233391u};
    const uint32_t results[]={35,46,42};
    for(unsigned n=0;n<3;n++) {
        init(aliases[n]);c.r[1]=5;c.r[2]=11;c.r[3]=7;
        CHECK(arm_step(&c)==ARM_OK && c.r[(aliases[n]>>16)&15u]==results[n]);
    }
    const uint32_t invalid[]={
        0xe00f0391u,0xe0020f91u,0xe002039fu, /* PC in each MUL role */
        0xe0021391u,0xe0010391u,             /* reserved addend / d == first source */
        0xe02f4391u,0xe0224f91u,0xe022439fu,0xe022f391u,0xe0214391u
    };
    for(unsigned n=0;n<sizeof invalid/sizeof invalid[0];n++) {
        init(invalid[n]);c.r[1]=5;c.r[2]=11;c.r[3]=7;
        arm_cpu_t before=c;
        CHECK(arm_step(&c)==ARM_UNDEFINED && !memcmp(&before,&c,sizeof c) && writes==0);
        /* A failed condition also skips invalid operand combinations. */
        init(invalid[n]&0x0fffffffu);c.r[1]=5;
        CHECK(arm_step(&c)==ARM_OK && c.r[15]==4 && c.cycles==1 && c.r[1]==5 && writes==0);
    }
    const arm_arch_t later[]={ARM_ARCH_V6_ARM1176,ARM_ARCH_V7_CORTEX_A8,ARM_ARCH_V7_SWIFT};
    for(unsigned n=0;n<3;n++) {
        init(0xe0110391u);CHECK(arm_reset_profile(&c,&bus,later[n]));
        c.r[1]=0xffffffffu;c.r[3]=2;c.cpsr|=ARM_CPSR_C|ARM_CPSR_V|ARM_CPSR_Z;
        uint32_t before=c.cpsr;
        CHECK(arm_step(&c)==ARM_OK && c.r[1]==0xfffffffeu && c.r[15]==4);
        CHECK(c.cpsr==((before&~ARM_CPSR_Z)|ARM_CPSR_N));
    }
}
static uint32_t long_multiply(unsigned kind, bool set, unsigned lo,
                              unsigned hi, unsigned m, unsigned s) {
    return 0xe0800090u | (kind<<21) | ((uint32_t)set<<20) |
        (hi<<16) | (lo<<12) | (s<<8) | m;
}
static void test_multiply_long(void) {
    /* Independently calculated products: signed high words, zero low words,
     * and values whose product crosses both the 32- and 63-bit boundaries. */
    static const struct { uint32_t m,s; uint64_t u,i; } cases[]={
        {0,0xffffffffu,0,0},
        {1,1,1,1},
        {0xffffffffu,2,UINT64_C(0x1fffffffe),UINT64_C(0xfffffffffffffffe)},
        {0xffffffffu,0xffffffffu,UINT64_C(0xfffffffe00000001),1},
        {0x80000000u,2,UINT64_C(0x100000000),UINT64_C(0xffffffff00000000)},
        {0x80000000u,0x80000000u,UINT64_C(0x4000000000000000),UINT64_C(0x4000000000000000)},
        {0x7fffffffu,0x7fffffffu,UINT64_C(0x3fffffff00000001),UINT64_C(0x3fffffff00000001)},
        {0x80000000u,0xffffffffu,UINT64_C(0x7fffffff80000000),UINT64_C(0x80000000)},
        {0x10000u,0x10000u,UINT64_C(0x100000000),UINT64_C(0x100000000)}
    };
    static const uint64_t addends[]={0,UINT64_C(0xffffffff),UINT64_MAX,UINT64_C(0x8000000000000000)};
    const arm_arch_t profiles[]={PROFILE,ARM_ARCH_V6_ARM1176,ARM_ARCH_V7_CORTEX_A8,ARM_ARCH_V7_SWIFT};
    for(unsigned p=0;p<4;p++)for(unsigned n=0;n<sizeof cases/sizeof cases[0];n++)
    for(unsigned kind=0;kind<4;kind++)for(unsigned set=0;set<2;set++)
    for(unsigned flags=0;flags<16;flags++)for(unsigned a=0;a<(kind&1u?4u:1u);a++) {
        init(long_multiply(kind,set!=0,0,1,2,3));
        CHECK(arm_reset_profile(&c,&bus,profiles[p]));
        c.r[0]=(uint32_t)addends[a];c.r[1]=(uint32_t)(addends[a]>>32);
        c.r[2]=cases[n].m;c.r[3]=cases[n].s;c.cpsr|=flags<<28;
        c.excl_valid=true;c.excl_addr=0x300;c.a8_excl_size=4;
        arm_cpu_t expected=c;
        uint64_t result=(kind&2u?cases[n].i:cases[n].u)+(kind&1u?addends[a]:0);
        expected.r[0]=(uint32_t)result;expected.r[1]=(uint32_t)(result>>32);
        expected.r[15]=4;expected.cycles=1;
        if(set)expected.cpsr=(expected.cpsr&~(ARM_CPSR_N|ARM_CPSR_Z))|
            ((result>>63)?ARM_CPSR_N:0)|(result?0:ARM_CPSR_Z);
        CHECK(arm_step(&c)==ARM_OK);
        CHECK(!memcmp(&c,&expected,sizeof c));
        CHECK(reads==1 && read_size==4 && last_read==0 && writes==0 && !fault);
    }
    /* All sources and the addend must be read before either destination is
     * written. First-source aliases remain refused only on the v4T profile. */
    static const struct { unsigned m,s; uint64_t product; } aliases[]={
        {2,0,55},{2,1,77},{2,2,121},{0,2,55},{1,2,77},{0,1,35}
    };
    for(unsigned p=0;p<4;p++)for(unsigned n=0;n<6;n++)for(unsigned kind=0;kind<4;kind++) {
        init(long_multiply(kind,true,0,1,aliases[n].m,aliases[n].s));
        CHECK(arm_reset_profile(&c,&bus,profiles[p]));
        c.r[0]=5;c.r[1]=7;c.r[2]=11;c.cpsr|=ARM_CPSR_C|ARM_CPSR_V|ARM_CPSR_N|ARM_CPSR_Z;
        arm_cpu_t expected=c;
        bool refuse=p==0 && aliases[n].m<2;
        uint64_t result=aliases[n].product+(kind&1u?UINT64_C(0x700000005):0);
        if(!refuse) {
            expected.r[0]=(uint32_t)result;expected.r[1]=(uint32_t)(result>>32);
            expected.cpsr&=~(ARM_CPSR_N|ARM_CPSR_Z);expected.r[15]=4;expected.cycles=1;
        }
        CHECK(arm_step(&c)==(refuse?ARM_UNDEFINED:ARM_OK));
        CHECK(!memcmp(&c,&expected,sizeof c) && reads==1 && writes==0 && !fault);
    }
    /* PC in each role and equal destinations refuse atomically, including
     * S/accumulate variants. EQ with Z clear skips those same encodings. */
    const unsigned invalid[][4]={{15,1,2,3},{0,15,2,3},{0,1,15,3},{0,1,2,15},{1,1,2,3},{0,1,0,3},{0,1,1,3}};
    for(unsigned n=0;n<7;n++)for(unsigned kind=0;kind<4;kind++)
    for(unsigned set=0;set<2;set++)for(unsigned skip=0;skip<2;skip++) {
        uint32_t insn=long_multiply(kind,set!=0,invalid[n][0],invalid[n][1],invalid[n][2],invalid[n][3]);
        init(skip?insn&0x0fffffffu:insn);
        c.r[0]=5;c.r[1]=7;c.r[2]=11;c.r[3]=13;c.excl_valid=true;
        arm_cpu_t expected=c;
        if(skip){expected.r[15]=4;expected.cycles=1;}
        CHECK(arm_step(&c)==(skip?ARM_OK:ARM_UNDEFINED));
        CHECK(!memcmp(&c,&expected,sizeof c) && reads==1 && writes==0 && !fault);
    }
    /* Existing bus failure wins before instruction fetch or multiplication. */
    init(long_multiply(3,true,0,1,2,3));c.r[2]=11;c.r[3]=13;fault=true;
    arm_cpu_t before=c;
    CHECK(arm_step(&c)==ARM_HALT && !memcmp(&c,&before,sizeof c) && !reads && !writes);
}
static void test_bus_failure(void) {
    const uint32_t insns[] = {0xe5901000u,0xe5801000u,0xe8b00006u};
    for (unsigned i=0;i<3;++i) {
        init(insns[i]); c.r[0]=sizeof ram; c.r[1]=0x12345678u; c.r[2]=0xaabbccddu;
        uint32_t regs[16]; memcpy(regs,c.r,sizeof regs);
        CHECK(arm_step(&c)==ARM_HALT && fault && c.cycles==0);
        CHECK(!memcmp(regs,c.r,sizeof regs) && c.cpsr==0xd3u);
        unsigned before=reads;
        CHECK(arm_step(&c)==ARM_HALT && reads==before && c.cycles==0);
    }
}
static void test_original_halfword_store(void) {
    init(0xe18520b0u); /* Original IOP 0x28b4: STRH r2,[r5,r0]. */
    c.r[5]=128;c.r[0]=0x1b8;c.r[2]=0xabcdffffu;c.cpsr|=0xf0000000u;
    memset(ram+0x237,0xa5,4);uint32_t psr=c.cpsr;
    CHECK(arm_step(&c)==ARM_OK);
    CHECK(c.r[15]==4 && c.cycles==1 && writes==1 && reads==1);
    CHECK(ram[0x237]==0xa5 && ram[0x238]==0xff && ram[0x239]==0xff && ram[0x23a]==0xa5);
    CHECK(c.r[5]==128 && c.r[0]==0x1b8 && c.r[2]==0xabcdffffu && c.cpsr==psr);
}
/* DDI0100I A4.1.28-30/104, A5.3: STRH, LDRH, LDRSB, LDRSH; the six
 * forms are immediate/register offset, pre-indexed, and post-indexed. */
static uint32_t extra(unsigned op,unsigned form,bool up,unsigned rn,unsigned rd,unsigned offset) {
    const uint32_t operations[]={0xb0u,0x1000b0u,0x1000d0u,0x1000f0u};
    bool immediate=!(form&1u);
    return 0xe0000000u|operations[op]|(form<4?1u<<24:0)|(up?1u<<23:0)|
        (immediate?1u<<22:0)|((form==2 || form==3)?1u<<21:0)|(rn<<16)|(rd<<12)|
        (immediate?((offset&0xf0u)<<4)|(offset&15u):offset);
}
static uint32_t loaded(unsigned op,uint32_t value) {
    if(op==2)return (value&0xffu)|((value&0x80u)?0xffffff00u:0);
    return (value&0xffffu)|((op==3 && (value&0x8000u))?0xffff0000u:0);
}
static void test_extra_transfers(void) {
    const unsigned offsets[]={0,2,14,16,0x5a,0xfe};
    const uint32_t values[]={0,0x7f,0x80,0x7fff,0x8000,0xffff,0x89abcdefu};
    for(unsigned op=0;op<4;++op)for(unsigned form=0;form<6;++form)
    for(unsigned up=0;up<2;++up)for(unsigned n=0;n<6;++n)for(unsigned v=0;v<7;++v) {
        unsigned offset=offsets[n],width=op==2?1:2;
        init(extra(op,form,up,1,2,(form&1u)?3:offset));
        c.r[1]=512;c.r[2]=values[v];c.r[3]=offset;c.cpsr|=0xf0000000u;
        uint32_t changed=up?512+offset:512-offset,address=form<4?changed:512,psr=c.cpsr;
        memset(ram+address-1,0xa5,4);if(op)put(address,values[v],width);
        CHECK(arm_step(&c)==ARM_OK && c.r[15]==4 && c.cycles==1);
        CHECK(c.r[1]==(form<2?512:changed) && c.r[3]==offset && c.cpsr==psr);
        CHECK(c.r[2]==(op?loaded(op,values[v]):values[v]) && !fault);
        CHECK(ram[address-1]==0xa5 && ram[address+width]==0xa5);
        if(op)CHECK(reads==2 && writes==0 && read_size==width && last_read==address);
        else {
            CHECK(reads==1 && writes==1 && write_size==2 && last_write==address);
            CHECK(ram[address]==(uint8_t)values[v] && ram[address+1]==(uint8_t)(values[v]>>8));
        }
    }
    /* Unsigned halfword and signed-halfword loads differ at both sign edges;
     * signed byte also accepts odd addresses. Post-index may leave an odd base. */
    for(unsigned op=0;op<4;++op) {
        init(extra(op,4,true,1,2,1));c.r[1]=128;c.r[2]=0x8080;put(128,0x8080,2);
        CHECK(arm_step(&c)==ARM_OK && c.r[1]==129 && c.r[2]==(op?loaded(op,0x8080):0x8080));
    }
    init(extra(2,0,true,1,2,1));c.r[1]=128;ram[129]=0x80;
    CHECK(arm_step(&c)==ARM_OK && c.r[2]==0xffffff80u && read_size==1 && last_read==129);
}
static void test_extra_aliases(void) {
    for(unsigned op=0;op<4;++op) {
        /* PC-relative base uses instruction address +8, without writeback. */
        init(extra(op,0,true,15,2,120));c.r[2]=0x8123;put(128,0x8123,2);
        CHECK(arm_step(&c)==ARM_OK && c.r[15]==4 && c.r[2]==(op?loaded(op,0x8123):0x8123));
        /* Rn==Rd and Rn==Rm are legal without writeback; Rd==Rm is legal
         * even with writeback. Every address uses the original operands. */
        init(extra(op,0,true,1,1,2));c.r[1]=128;put(130,0x8123,2);
        CHECK(arm_step(&c)==ARM_OK && c.r[1]==(op?loaded(op,0x8123):128));
        if(!op)CHECK(ram[130]==128 && ram[131]==0);
        init(extra(op,1,true,1,2,1));c.r[1]=128;c.r[2]=0x8123;put(256,0x8123,2);
        CHECK(arm_step(&c)==ARM_OK && c.r[1]==128 && c.r[2]==(op?loaded(op,0x8123):0x8123));
        init(extra(op,3,true,1,2,2));c.r[1]=128;c.r[2]=2;put(130,0x8123,2);
        CHECK(arm_step(&c)==ARM_OK && c.r[1]==130 && c.r[2]==(op?loaded(op,0x8123):2));
        if(!op)CHECK(ram[130]==2 && ram[131]==0);
    }
    const arm_arch_t later[]={ARM_ARCH_V6_ARM1176,ARM_ARCH_V7_CORTEX_A8,ARM_ARCH_V7_SWIFT};
    for(unsigned p=0;p<3;++p)for(unsigned op=0;op<4;++op)for(unsigned form=3;form<=5;form+=2) {
        init(extra(op,form,true,1,2,1));CHECK(arm_reset_profile(&c,&bus,later[p]));
        c.r[1]=128;c.r[2]=0x8123;put(form==3?256:128,0x8123,2);
        CHECK(arm_step(&c)==ARM_OK && c.r[1]==256 && c.r[2]==(op?loaded(op,0x8123):0x8123));
    }
}
static void test_extra_refusals(void) {
    for(unsigned op=0;op<4;++op) {
        const uint32_t bad[]={
            extra(op,0,true,1,15,0),extra(op,1,true,1,2,15),
            extra(op,1,true,1,2,3)|0x100u, /* Register encoding's SBZ field. */
            extra(op,4,true,1,2,2)|(1u<<21), /* P=0 W=1. */
            extra(op,2,true,15,2,2),extra(op,4,true,15,2,2),
            extra(op,2,true,1,1,2),extra(op,4,true,1,1,2),
            extra(op,3,true,1,2,1),extra(op,5,true,1,2,1) /* Pre-v6 Rn==Rm. */
        };
        for(unsigned n=0;n<sizeof bad/sizeof bad[0];++n) {
            init(bad[n]);c.r[1]=128;c.r[2]=0x8123;c.r[3]=2;arm_cpu_t before=c;
            CHECK(arm_step(&c)==ARM_UNDEFINED && !memcmp(&before,&c,sizeof c));
            CHECK(reads==1 && writes==0 && !fault);
            init(bad[n]&0x0fffffffu);c.r[1]=128;c.r[2]=0x8123;
            CHECK(arm_step(&c)==ARM_OK && c.r[15]==4 && c.cycles==1 && reads==1 && !writes);
        }
        if(op!=2) {
            init(extra(op,0,true,1,2,0));c.r[1]=129;c.r[2]=0x8123;arm_cpu_t before=c;
            CHECK(arm_step(&c)==ARM_UNDEFINED && !memcmp(&before,&c,sizeof c));
            CHECK(reads==1 && !writes && !fault);
        }
    }
}
static void test_extra_faults(void) {
    for(unsigned op=0;op<4;++op)for(unsigned form=0;form<6;++form) {
        init(extra(op,form,true,1,2,(form&1u)?3:2));
        c.r[1]=form<4?sizeof ram-2:sizeof ram;c.r[2]=0x89abcdef;c.r[3]=2;
        uint32_t before[16],psr=c.cpsr;memcpy(before,c.r,sizeof before);
        CHECK(arm_step(&c)==ARM_HALT && fault && !c.cycles && !c.abort_pending);
        CHECK(!memcmp(before,c.r,sizeof before) && c.cpsr==psr);
        CHECK(c.abort_fsr==ARM_MMU_BUS_FAILURE && c.abort_far==sizeof ram);
        CHECK(reads==(op?2u:1u) && writes==(op?0u:1u));
        unsigned r=reads,w=writes;
        CHECK(arm_step(&c)==ARM_HALT && reads==r && writes==w && !c.cycles);
        init(extra(op,form,true,1,2,(form&1u)?3:2)&0x0fffffffu);
        c.r[1]=sizeof ram;c.r[2]=0x89abcdef;c.r[3]=2;
        CHECK(arm_step(&c)==ARM_OK && c.r[1]==sizeof ram && c.r[2]==0x89abcdef && !fault && reads==1 && !writes);
    }
}
int main(void) {
    if (!arm_reset_profile(&c,&bus,PROFILE)) {
        puts("FAIL: explicit ARMv4T profile is unavailable"); return 1;
    }
    test_reset(); test_bootstrap(); test_pc_loads(); test_boundaries();
    test_psr_and_irq(); test_multiply(); test_multiply_long(); test_bus_failure();
    test_original_halfword_store();
    test_extra_transfers();test_extra_aliases();test_extra_refusals();test_extra_faults();
    printf("arm_v4t: %u passed, %u failed\n",passed,failed);
    return failed ? 1 : 0;
}
