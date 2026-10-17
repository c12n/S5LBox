/* Explicit, partial ARMv4T profile: architectural boundaries and memory effects.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "arm.h"
#include "jit.h"
#include <stdio.h>
#include <string.h>

#define PROFILE ARM_ARCH_V4T
static arm_cpu_t c;
static uint8_t ram[4096];
static unsigned passed, failed, reads, writes;
static bool fault;
#define CHECK(x) do { if (x) passed++; else { failed++; \
    printf("FAIL %s:%d: %s\n", __func__, __LINE__, #x); } } while (0)
static bool access_failed(void *ctx) { (void)ctx; return fault; }
static uint32_t read_width(uint32_t a, unsigned width) {
    reads++;
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
static void w32(void *ctx, uint32_t a, uint32_t v) { (void)ctx; writes++; put(a, v, 4); }
static void w16(void *ctx, uint32_t a, uint16_t v) { (void)ctx; writes++; put(a, v, 2); }
static void w8(void *ctx, uint32_t a, uint8_t v) { (void)ctx; writes++; put(a, v, 1); }
static arm_bus_t bus = { .read32=r32, .read16=r16, .read8=r8,
    .write32=w32, .write16=w16, .write8=w8, .access_failed=access_failed };
static void init(uint32_t insn) {
    memset(ram, 0, sizeof ram); fault=false; reads=writes=0;
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
        0xe0810392u, /* UMULL */ 0xe1012090u, /* SWP */
        0xe1d010b0u, /* LDRH */ 0xef000000u, /* SWI */
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
int main(void) {
    if (!arm_reset_profile(&c,&bus,PROFILE)) {
        puts("FAIL: explicit ARMv4T profile is unavailable"); return 1;
    }
    test_reset(); test_bootstrap(); test_pc_loads(); test_boundaries();
    test_psr_and_irq(); test_multiply(); test_bus_failure();
    printf("arm_v4t: %u passed, %u failed\n",passed,failed);
    return failed ? 1 : 0;
}
