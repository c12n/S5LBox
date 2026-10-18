/* IOP instructions access the shared, explicitly attached NAND controllers.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920.h"
#include <stdio.h>
#include <string.h>
#define BASE UINT32_C(0x41000000)
#define GATE(n) (S5L8920_CLOCK_GATE_BASE+4u*(n))
static s5l8920_t board;
static s5l8920_iop_t iop;
static s5l8920_fmi_t fmi[S5L8920_FMI_COUNT];
static unsigned passed,failed;
#define CHECK(x) do { if(x)passed++;else {failed++;printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);} } while(0)
static void wr(uint32_t a,uint32_t v) { board.bus.write32(&board,a,v); }
static void init(void) {
    s5l8920_free(&board);CHECK(s5l8920_init(&board));s5l8920_iop_init(&iop);
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_RESET_GATE,15));
    CHECK(s5l8920_clock_gate_configure(&board,S5L8920_IOP_DEPENDENCY_GATE,15));
    CHECK(s5l8920_iop_attach(&board,&iop));
    for(unsigned n=0;n<S5L8920_FMI_COUNT;++n) {
        memset(&fmi[n],0,sizeof fmi[n]);CHECK(s5l8920_fmi_init(&fmi[n]));
        CHECK(s5l8920_clock_gate_configure(&board,S5L8920_FMI_GATE+n,15));
        CHECK(s5l8920_fmi_attach(&board,n,&fmi[n]));
    }
    wr(GATE(S5L8920_IOP_RESET_GATE),0x8000000fu);
    wr(GATE(S5L8920_IOP_RESET_GATE),15);
    wr(S5L8920_IOP_BASE+0x118,S5L8920_IOP_BASE);
    wr(S5L8920_IOP_BASE+0x110,BASE);
    wr(S5L8920_IOP_BASE+0x114,0x3ffff000u);
    wr(S5L8920_IOP_BASE+0x100,1);
    wr(BASE+4,0xe5801000u);wr(BASE+8,0xe5901000u); /* STR/LDR r1,[r0]. */
    wr(BASE+12,0xe5c01000u);wr(BASE+16,0xe5d01000u); /* STRB/LDRB. */
    wr(BASE+20,0xe1c010b0u);wr(BASE+24,0xe1d010b0u); /* STRH/LDRH. */
    CHECK(!board.bus_failure.reason && iop.running);
}
static void bindings_cleared(void) {
    CHECK(!iop.servicing && !iop.memory && !iop.signal_ap && !iop.timebase &&
          !iop.fmi && !iop.sdhc && !iop.gate && !iop.memory_owner);
}
static void prepare(unsigned pc,uint32_t address,uint32_t value) {
    iop.cpu.r[15]=pc;iop.cpu.r[0]=address;iop.cpu.r[1]=value;
}
static void store(uint32_t address,uint32_t value) {
    prepare(4,address,value);CHECK(s5l8920_iop_board_service(&board)==ARM_OK);
    bindings_cleared();
}
static uint32_t load(uint32_t address) {
    prepare(8,address,0xdeadbeefu);CHECK(s5l8920_iop_board_service(&board)==ARM_OK);
    bindings_cleared();return iop.cpu.r[1];
}
static uint8_t *memory(void *owner,uint32_t address,uint32_t size) {
    s5l8920_t *m=owner;
    if(address<BASE || size>0x1000 || address-BASE>0x1000-size)return NULL;
    return m->ram+address-S5L8920_RAM_BASE;
}
static void test_shared_controllers(void) {
    init();arm_cpu_t ap=board.cpu;
    for(unsigned n=0;n<S5L8920_FMI_COUNT;++n) {
        uint32_t a=S5L8920_FMI_BASE+n*S5L8920_FMI_STRIDE;
        store(a+0x408,0x1234u+n); /* Timing can be programmed before enable. */
        CHECK(fmi[n].timing==0x1234u+n && !fmi[n].enabled);
        store(a+0x400,1);CHECK(load(a+0x408)==0x1234u+n);
        CHECK(board.bus.read32(&board,a+0x408)==0x1234u+n);
        wr(a+0x408,0x5678u+n);CHECK(load(a+0x408)==0x5678u+n);
        store(a+0x40c,1);store(a+0x414,0xff);store(a+0x410,1);
        s5l8920_fmi_t pending=fmi[n];
        for(unsigned k=0;k<3;++k)CHECK(load(a+0x444)==0);
        CHECK(fmi[n].pending && !fmi[n].commands && !memcmp(&pending,&fmi[n],sizeof pending));
        CHECK(!s5l8920_fmi_board_service(&board,n,NULL,NULL));
        CHECK(!memcmp(&pending,&fmi[n],sizeof pending));
    }
    CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason &&
          !iop.bytes_written && !board.timebase_ticks);
}
static void check_refusal(s5l8920_iop_failure_reason_t reason) {
    arm_cpu_t cpu=iop.cpu,ap=board.cpu;
    s5l8920_fmi_t before[S5L8920_FMI_COUNT];memcpy(before,fmi,sizeof before);
    s5l8920_bus_failure_t ap_failure=board.bus_failure;
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(iop.failure.reason==reason && iop.failure.pc==cpu.r[15] &&
          iop.failure.address==cpu.r[0] && iop.failure.write==(cpu.r[15]==4 || cpu.r[15]==12 || cpu.r[15]==20));
    CHECK(iop.failure.size==(cpu.r[15]>=20?2u:cpu.r[15]>=12?1u:4u));
    uint32_t operand=cpu.r[15]==12?(cpu.r[1]&255u):cpu.r[15]==20?(cpu.r[1]&65535u):cpu.r[1];
    CHECK(iop.failure.value==(iop.failure.write?operand:0));
    arm_cpu_t after=iop.cpu;
    CHECK(!after.abort_pending && after.abort_fsr==ARM_MMU_BUS_FAILURE && after.abort_far==cpu.r[0]);
    after.abort_pending=cpu.abort_pending;after.abort_fsr=cpu.abort_fsr;after.abort_far=cpu.abort_far;
    /* A refused load still records its attempted host read-cache lookup. */
    CHECK(after.dread_misses==cpu.dread_misses+(iop.failure.write?0u:1u));
    after.dread_misses=cpu.dread_misses;
    CHECK(!memcmp(&cpu,&after,sizeof cpu) && !memcmp(&ap,&board.cpu,sizeof ap));
    CHECK(!memcmp(before,fmi,sizeof before) && !memcmp(&ap_failure,&board.bus_failure,sizeof ap_failure));
    CHECK(iop.running && !iop.bytes_written);bindings_cleared();
    s5l8920_iop_t stopped=iop;
    iop.bus.write32(&iop,S5L8920_FMI_BASE+0x400,1);
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT && !memcmp(&stopped,&iop,sizeof iop));
    CHECK(!memcmp(before,fmi,sizeof before));
}
static void test_refusals(void) {
    const struct {unsigned pc;uint32_t offset,value;} bad[]={
        {4,0x400,0},{4,0x400,2},{4,0x400,0xffffffffu},
        {8,0x400,0},{8,0x408,0},{8,0x448,0}, /* No fabricated readbacks. */
        {4,0x40c,3},{4,0x440,1},{4,0x814,1},{4,0x404,0},{8,0x404,0},
        {4,0x14,0},{8,0x18,0},{4,0x818,0},{8,0xffc,0},
        {12,0x400,1},{16,0x444,0},{12,0x401,1},{20,0x400,1},{24,0x444,0}
    };
    for(unsigned bank=0;bank<S5L8920_FMI_COUNT;++bank) {
        uint32_t a=S5L8920_FMI_BASE+bank*S5L8920_FMI_STRIDE;
        for(unsigned n=0;n<sizeof bad/sizeof bad[0];++n) {
            init();store(a+0x400,1);
            prepare(bad[n].pc,a+bad[n].offset,bad[n].value);
            check_refusal(S5L8920_IOP_REGISTER_REFUSED);
        }
        /* Missing attachment, closed clock, asserted reset, and unconfigured
         * gate are independent refusals, even when a controller exists. */
        for(unsigned condition=0;condition<4;++condition) {
            init();
            if(condition==0)board.fmi[bank]=NULL;
            if(condition==1)wr(GATE(S5L8920_FMI_GATE+bank),0);
            if(condition==2)wr(GATE(S5L8920_FMI_GATE+bank),0x8000000fu);
            if(condition==3)board.clock_gate[S5L8920_FMI_GATE+bank].configured=false;
            prepare(4,a+0x400,1);check_refusal(S5L8920_IOP_REGISTER_REFUSED);
        }
    }
    const uint32_t outside[]={S5L8920_FMI_BASE-4,S5L8920_FMI_BASE+0x1000,
        S5L8920_FMI_BASE+S5L8920_FMI_COUNT*S5L8920_FMI_STRIDE};
    for(unsigned n=0;n<sizeof outside/sizeof outside[0];++n) {
        init();prepare(4,outside[n],1);check_refusal(S5L8920_IOP_UNMAPPED);
    }
}
static unsigned capability_calls;
static bool capability_refuse;
static bool capability(void *owner,unsigned bank,uint32_t offset,bool write,uint32_t *value) {
    CHECK(owner==&board && bank==1 && offset==0x408 && iop.servicing && iop.fmi==capability);
    capability_calls++;s5l8920_iop_t before=iop;
    CHECK(s5l8920_iop_service_with_fmi(&iop,memory,NULL,NULL,capability,owner)==ARM_HALT);
    CHECK(!s5l8920_iop_reset_line(&iop,true) && !s5l8920_iop_ram_configure(&iop,0x1000));
    CHECK(!memcmp(&before,&iop,sizeof iop));
    if(capability_refuse)return false;
    return write?s5l8920_fmi_write(&fmi[bank],offset,*value):s5l8920_fmi_read(&fmi[bank],offset,value);
}
static void test_borrowed_lifetime(void) {
    uint32_t a=S5L8920_FMI_BASE+S5L8920_FMI_STRIDE;
    init();store(a+0x400,1);capability_calls=0;capability_refuse=false;
    prepare(4,a+0x408,0x87654321u);
    CHECK(s5l8920_iop_service_with_fmi(&iop,memory,NULL,NULL,capability,&board)==ARM_OK);
    CHECK(fmi[1].timing==0x87654321u && capability_calls==1);bindings_cleared();
    prepare(8,a+0x408,0);
    CHECK(s5l8920_iop_service_with_fmi(&iop,memory,NULL,NULL,capability,&board)==ARM_OK);
    CHECK(iop.cpu.r[1]==0x87654321u && capability_calls==2);bindings_cleared();
    capability_refuse=true;prepare(8,a+0x408,0xdeadbeefu);
    CHECK(s5l8920_iop_service_with_fmi(&iop,memory,NULL,NULL,capability,&board)==ARM_HALT);
    CHECK(iop.cpu.r[1]==0xdeadbeefu && iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED);
    CHECK(capability_calls==3 && !board.bus_failure.reason);bindings_cleared();
    /* Every earlier entry point still withholds FMI access. */
    for(unsigned api=0;api<4;++api) {
        init();prepare(4,a+0x408,1);
        arm_status_t result=api==0?s5l8920_iop_service(&iop,memory,&board):
            api==1?s5l8920_iop_service_with_signal(&iop,memory,NULL,&board):
            api==2?s5l8920_iop_service_with_timebase(&iop,memory,NULL,NULL,&board):
            s5l8920_iop_service_with_fmi(&iop,memory,NULL,NULL,NULL,&board);
        CHECK(result==ARM_HALT && iop.failure.reason==S5L8920_IOP_UNMAPPED);
        CHECK(!fmi[1].programmed && capability_calls==3);bindings_cleared();
    }
    init();wr(BASE+4,0xee100f10u); /* Unsupported CP15 must also release bindings. */
    prepare(4,a+0x408,1);
    CHECK(s5l8920_iop_service_with_fmi(&iop,memory,NULL,NULL,capability,&board)==ARM_UNDEFINED);
    CHECK(!iop.failure.reason && capability_calls==3);bindings_cleared();
}
static unsigned direct_size;
static uint32_t direct_address;
static bool direct_write;
static uint8_t *bus_probe(void *owner,uint32_t address,uint32_t size) {
    /* Exercise raw bus width/alignment without the ARMv4T decoder aligning a
     * word or refusing its unsupported halfword encoding before device access. */
    if(direct_write) {
        if(direct_size==2)iop.bus.write16(&iop,direct_address,1);
        else iop.bus.write32(&iop,direct_address,1);
    } else {
        if(direct_size==2)(void)iop.bus.read16(&iop,direct_address);
        else (void)iop.bus.read32(&iop,direct_address);
    }
    return memory(owner,address,size);
}
static void test_bus_alignment(void) {
    for(unsigned n=0;n<4;++n) {
        init();wr(BASE,0xe1a00000u);direct_size=n<2?2:4;direct_write=(n&1u)!=0;
        direct_address=S5L8920_FMI_BASE+S5L8920_FMI_STRIDE+0x408+(n<2?0:1);
        unsigned before=capability_calls;
        CHECK(s5l8920_iop_service_with_fmi(&iop,bus_probe,NULL,NULL,capability,&board)==ARM_HALT);
        CHECK(iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED &&
              iop.failure.address==direct_address && iop.failure.size==direct_size &&
              iop.failure.write==direct_write && iop.cpu.cycles==0);
        CHECK(capability_calls==before && !fmi[1].programmed && !board.bus_failure.reason);
        bindings_cleared();
    }
}
static bool endpoint(void *owner,unsigned chip,uint8_t command,uint8_t address,uint8_t id[8]) {
    (void)id;CHECK(owner==&board && chip==0 && command==0xff && address==0);
    s5l8920_iop_t before=iop;arm_cpu_t ap=board.cpu;
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(!memcmp(&before,&iop,sizeof iop) && !memcmp(&ap,&board.cpu,sizeof ap));
    CHECK(!board.bus_failure.reason && !fmi[1].enabled);return true;
}
static void test_reset_and_reentry(void) {
    init();uint32_t a=S5L8920_FMI_BASE;
    store(a+0x400,1);store(a+0x408,0x1234);
    wr(GATE(S5L8920_FMI_GATE),0x8000000fu);
    CHECK(fmi[0].reset_asserted && !fmi[0].enabled && !fmi[0].programmed);
    wr(GATE(S5L8920_FMI_GATE),15);CHECK(!fmi[0].reset_asserted && !fmi[0].enabled);
    store(a+0x400,1);store(a+0x408,7);CHECK(load(a+0x408)==7);
    store(a+0x40c,1);store(a+0x414,0xff);store(a+0x410,1);
    prepare(4,a+S5L8920_FMI_STRIDE+0x400,1);
    CHECK(s5l8920_fmi_board_service(&board,0,endpoint,&board));
    CHECK(fmi[0].commands==1 && fmi[0].status==1 && !fmi[1].enabled);
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && fmi[1].enabled);bindings_cleared();
    /* An AP fault prevents service without replacing either CPU or latch. */
    wr(0xffffffffu,1);CHECK(board.bus_failure.reason);
    s5l8920_bus_failure_t failure=board.bus_failure;s5l8920_iop_t before=iop;
    CHECK(s5l8920_iop_board_service(&board)==ARM_HALT);
    CHECK(!memcmp(&failure,&board.bus_failure,sizeof failure) && !memcmp(&before,&iop,sizeof iop));
}
static void test_original_enable(void) {
    init();wr(BASE,0xe58e8400u); /* STR r8,[lr,#0x400], original IOP PC 0x2884. */
    iop.cpu.r[8]=1;iop.cpu.r[14]=S5L8920_FMI_BASE;
    board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;
    arm_cpu_t ap=board.cpu;
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK);
    CHECK(iop.cpu.cycles==1 && iop.cpu.r[15]==4 && !iop.failure.reason);
    CHECK(fmi[0].enabled && !fmi[1].enabled);
    CHECK(!fmi[0].pending && !fmi[0].status && !fmi[0].commands && !fmi[0].bytes_latched);
    CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason);
    CHECK(!iop.bytes_written && !iop.servicing && !iop.memory && !iop.memory_owner);
}
static void test_shared_reset_gates(void) {
    for(unsigned bank=0;bank<S5L8920_FMI_COUNT;bank++) {
        init();uint32_t a=S5L8920_FMI_BASE+bank*S5L8920_FMI_STRIDE;
        uint32_t gate=GATE(S5L8920_FMI_GATE+bank);
        store(a+0x400,1);store(a+0x408,0x1234);
        store(a+0x40c,1);store(a+0x414,0xff);store(a+0x410,1);
        CHECK(fmi[bank].pending && fmi[bank].enabled);
        board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;arm_cpu_t ap=board.cpu;
        s5l8920_fmi_t peer=fmi[bank^1u];
        CHECK(load(gate)==15);
        store(gate,0x8000000fu);
        CHECK(board.bus.read32(&board,gate)==0x8000000fu);
        CHECK(fmi[bank].reset_asserted && !fmi[bank].enabled &&
              !fmi[bank].pending && !fmi[bank].programmed && !fmi[bank].status);
        s5l8920_fmi_t held=fmi[bank];
        CHECK(load(gate)==0x8000000fu);store(gate,0x8000000fu);
        CHECK(!memcmp(&held,&fmi[bank],sizeof held));
        store(gate,15);CHECK(!fmi[bank].reset_asserted && !fmi[bank].enabled);
        store(a+0x400,1);store(a+0x408,0x5678);CHECK(load(a+0x408)==0x5678);
        store(gate,0);CHECK(board.bus.read32(&board,gate)==0);
        wr(gate,15);CHECK(load(gate)==15);
        CHECK(!memcmp(&peer,&fmi[bank^1u],sizeof peer));
        CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason &&
              !iop.bytes_written && !board.timebase_ticks);
    }
}
static void test_gate_guards(void) {
    for(unsigned bank=0;bank<2;bank++)for(unsigned condition=0;condition<12;condition++) {
        init();unsigned pc=4;uint32_t gate=GATE(S5L8920_FMI_GATE+bank),value=0x8000000fu;
        if(condition==0)board.fmi[bank]=NULL;
        if(condition==1)fmi[bank].initialized=false;
        if(condition==2)board.clock_gate[S5L8920_FMI_GATE+bank].configured=false;
        if(condition==3)value=0x80000001u;
        if(condition==4)value=0x8000001fu;
        if(condition==5)value=0x4000000fu;
        if(condition>=6 && condition<=9)pc=12+(condition-6)*4;
        if(condition==10) {
            gate=GATE(S5L8920_SDHC_GATE);
            CHECK(s5l8920_clock_gate_configure(&board,S5L8920_SDHC_GATE,15));
        }
        if(condition==11)gate=GATE(S5L8920_IOP_RESET_GATE);
        s5l8920_clock_gate_t before[S5L8920_CLOCK_GATE_COUNT];memcpy(before,board.clock_gate,sizeof before);
        prepare(pc,gate,value);check_refusal(S5L8920_IOP_REGISTER_REFUSED);
        CHECK(!memcmp(before,board.clock_gate,sizeof before));
    }
    const uint32_t outside[]={S5L8920_CLOCK_GATE_BASE-4,
        S5L8920_CLOCK_GATE_BASE+4*S5L8920_CLOCK_GATE_COUNT,0xbf100000u};
    for(unsigned n=0;n<3;n++) {
        init();prepare(8,outside[n],0);check_refusal(S5L8920_IOP_UNMAPPED);
    }
    /* All selected words share AP programming; no second shadow image.
     * Preserve supplied upper bits and leave unsupported self-reset refused. */
    init();board.cpu.excl_valid=true;board.cpu.a8_excl_size=4;arm_cpu_t ap=board.cpu;
    for(unsigned n=0;n<S5L8920_CLOCK_GATE_COUNT;n++) {
        if(!board.clock_gate[n].configured)CHECK(s5l8920_clock_gate_configure(&board,n,0x40000000u));
        uint32_t initial=board.clock_gate[n].value;
        CHECK(load(GATE(n))==initial);
        if(n==S5L8920_IOP_RESET_GATE)continue;
        store(GATE(n),initial|15u);CHECK(board.bus.read32(&board,GATE(n))==(initial|15u));
        wr(GATE(n),initial&~15u);
        if(n!=S5L8920_IOP_DEPENDENCY_GATE)CHECK(load(GATE(n))==(initial&~15u));
        wr(GATE(n),initial|15u);
    }
    CHECK(!memcmp(&ap,&board.cpu,sizeof ap) && !board.bus_failure.reason && !iop.bytes_written);
    /* Closing the execution dependency pauses subsequent instructions. */
    store(GATE(S5L8920_IOP_DEPENDENCY_GATE),0);
    prepare(8,GATE(S5L8920_FMI_GATE),0xdeadbeef);s5l8920_iop_t paused=iop;
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && !memcmp(&paused,&iop,sizeof paused));
    wr(GATE(S5L8920_IOP_DEPENDENCY_GATE),15);
    CHECK(s5l8920_iop_board_service(&board)==ARM_OK && iop.cpu.r[1]==15);bindings_cleared();
}
static unsigned gate_calls;
static uint32_t gate_value;
static bool gate_refuse;
static bool gate_capability(void *owner,unsigned index,bool write,uint32_t *value) {
    CHECK(owner==&board && index==S5L8920_CLOCK_GATE_COUNT-1 && iop.servicing && iop.gate==gate_capability);
    gate_calls++;s5l8920_iop_t before=iop;
    CHECK(s5l8920_iop_service_with_gates(&iop,memory,NULL,NULL,NULL,NULL,gate_capability,owner)==ARM_HALT);
    CHECK(!s5l8920_iop_reset_line(&iop,true) && !memcmp(&before,&iop,sizeof before));
    if(gate_refuse)return false;
    if(write)gate_value=*value;else *value=gate_value;
    return true;
}
static void test_gate_lifetime(void) {
    uint32_t a=GATE(S5L8920_CLOCK_GATE_COUNT-1);init();gate_calls=0;gate_refuse=false;
    prepare(4,a,15);
    CHECK(s5l8920_iop_service_with_gates(&iop,memory,NULL,NULL,NULL,NULL,gate_capability,&board)==ARM_OK);
    CHECK(gate_value==15 && gate_calls==1);bindings_cleared();
    prepare(8,a,0);
    CHECK(s5l8920_iop_service_with_gates(&iop,memory,NULL,NULL,NULL,NULL,gate_capability,&board)==ARM_OK);
    CHECK(iop.cpu.r[1]==15 && gate_calls==2);bindings_cleared();
    gate_refuse=true;prepare(8,a,0xdeadbeef);
    CHECK(s5l8920_iop_service_with_gates(&iop,memory,NULL,NULL,NULL,NULL,gate_capability,&board)==ARM_HALT);
    CHECK(iop.cpu.r[1]==0xdeadbeef && gate_calls==3 && iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED);bindings_cleared();
    for(unsigned api=0;api<7;api++) {
        init();prepare(8,a,0xdeadbeef);
        arm_status_t status=api==0?s5l8920_iop_service(&iop,memory,&board):
            api==1?s5l8920_iop_service_with_signal(&iop,memory,NULL,&board):
            api==2?s5l8920_iop_service_with_timebase(&iop,memory,NULL,NULL,&board):
            api==3?s5l8920_iop_service_with_fmi(&iop,memory,NULL,NULL,NULL,&board):
            api==4?s5l8920_iop_service_with_sdhc(&iop,memory,NULL,NULL,NULL,NULL,&board):
            s5l8920_iop_service_with_gates(&iop,api==5?memory:NULL,NULL,NULL,NULL,NULL,NULL,&board);
        CHECK(status==ARM_HALT && iop.failure.reason==(api==6?S5L8920_IOP_NO_FAILURE:S5L8920_IOP_UNMAPPED));
        CHECK(iop.cpu.r[1]==0xdeadbeef && gate_calls==3);bindings_cleared();
    }
    for(unsigned n=0;n<4;n++) {
        init();wr(BASE,0xe1a00000);direct_size=n<2?2:4;direct_write=(n&1u)!=0;
        direct_address=a+(n<2?0:1);
        CHECK(s5l8920_iop_service_with_gates(&iop,bus_probe,NULL,NULL,NULL,NULL,gate_capability,&board)==ARM_HALT);
        CHECK(iop.failure.reason==S5L8920_IOP_REGISTER_REFUSED && iop.failure.address==direct_address &&
              iop.failure.size==direct_size && iop.failure.write==direct_write && gate_calls==3);bindings_cleared();
    }
    init();wr(BASE+4,0xee100f10);prepare(4,a,1);
    CHECK(s5l8920_iop_service_with_gates(&iop,memory,NULL,NULL,NULL,NULL,gate_capability,&board)==ARM_UNDEFINED);
    CHECK(gate_calls==3 && !iop.failure.reason);bindings_cleared();
    for(unsigned n=0;n<4;n++) {
        init();prepare(4,GATE(S5L8920_FMI_GATE),0x8000000f);
        if(n==0)iop.running=false;
        if(n==1)fmi[1].servicing=true;
        if(n==2)wr(0xffffffffu,1);
        if(n==3)iop.servicing=true;
        s5l8920_iop_t before=iop;s5l8920_fmi_t peer=fmi[0];
        s5l8920_bus_failure_t fault=board.bus_failure;
        CHECK(s5l8920_iop_board_service(&board)==(n==0?ARM_OK:ARM_HALT));
        CHECK(!memcmp(&before,&iop,sizeof iop) && !memcmp(&peer,&fmi[0],sizeof peer) &&
              !memcmp(&fault,&board.bus_failure,sizeof fault));
        fmi[1].servicing=false;iop.servicing=false;
    }
}
static unsigned page_calls,stream_calls;
static bool gate_page(void *owner,unsigned chip,uint32_t row) {
    CHECK(owner==&board && chip==0 && row==1);page_calls++;return true;
}
static s5l8920_fmi_data_result_t gate_stream(void *owner,unsigned chip,uint8_t *data,unsigned count) {
    CHECK(owner==&board && chip==0 && count==16);stream_calls++;memset(data,0xa5,count);
    return S5L8920_FMI_DATA_READY;
}
static void test_gate_fifo_reset(void) {
    for(unsigned bank=0;bank<2;bank++) {
        init();s5l8920_fmi_t *f=&fmi[bank];uint32_t a=S5L8920_FMI_BASE+bank*S5L8920_FMI_STRIDE;
        page_calls=stream_calls=0;CHECK(s5l8920_fmi_data_configure(f,512,32));
        store(a+0x400,1);store(a+0x408,0x11111);store(a+0x40c,1);store(a+0x414,0x3000);
        store(a+0x418,0x10000);store(a+0x41c,0);store(a+0x420,4);store(a+0x410,0xb);
        const s5l8920_fmi_endpoint_t cap={NULL,gate_page};
        CHECK(s5l8920_fmi_board_service_endpoint(&board,bank,&cap,&board));
        store(a+0x444,0xb);store(a+0x814,0);store(a+0x440,0);store(a+0x10,0);
        store(a,0xaa083);store(a+4,3);CHECK(s5l8920_fmi_data_service(f,gate_stream,&board));
        CHECK(f->fifo_count[0]==16 && f->sector_pending && f->stream_bytes==16 && f->commands==1);
        store(GATE(S5L8920_FMI_GATE+bank),0x8000000f);
        CHECK(f->reset_asserted && !f->sector_pending && !f->fifo_count[0] && !f->fifo_capacity[0] &&
              !f->fifo_count[1] && !f->fifo_capacity[1] && !f->page_issued && !f->programmed);
        CHECK(f->stream_bytes==16 && f->commands==1 && page_calls==1 && stream_calls==1);
        CHECK(s5l8920_fmi_data_service(f,gate_stream,&board) && stream_calls==1);
        store(GATE(S5L8920_FMI_GATE+bank),15);CHECK(!f->reset_asserted && !f->enabled);
    }
}
int main(void) {
    test_original_enable();test_shared_controllers();test_refusals();test_borrowed_lifetime();
    test_bus_alignment();test_reset_and_reentry();test_shared_reset_gates();test_gate_guards();
    test_gate_lifetime();test_gate_fifo_reset();s5l8920_free(&board);
    printf("s5l8920_iop_fmi: %u passed, %u failed\n",passed,failed);
    return failed?1:0;
}
