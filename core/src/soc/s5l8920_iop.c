/* Original AppleARM7M version1 control sequence, backed by an actual CPU.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_iop.h"
#include <string.h>

#define AP_VIC_BASE UINT32_C(0xbf200000)
static bool ap_vic_address(uint32_t address) {
    return address>=AP_VIC_BASE &&
        (address-AP_VIC_BASE)/S5L8920_IOP_VIC_STRIDE<S5L8920_IOP_VIC_COUNT &&
        (address-AP_VIC_BASE)%S5L8920_IOP_VIC_STRIDE<0x1000u;
}

static void refresh_interrupts(s5l8920_iop_t *i) {
    if(!i->vic_configured)return;
    bool irq=false,fiq=false;
    for(unsigned n=0;n<S5L8920_IOP_VIC_COUNT;++n) {
        irq=irq || pl192_irq(&i->vic[n]);fiq=fiq || pl192_fiq(&i->vic[n]);
    }
    i->cpu.irq_line=irq;i->cpu.fiq_line=fiq;
}
static bool vic_address(uint32_t address) {
    return address>=S5L8920_IOP_VIC_BASE &&
        (address-S5L8920_IOP_VIC_BASE)/S5L8920_IOP_VIC_STRIDE<S5L8920_IOP_VIC_COUNT &&
        (address-S5L8920_IOP_VIC_BASE)%S5L8920_IOP_VIC_STRIDE<0x1000u;
}
static pl192_t *vic_at(s5l8920_iop_t *i,uint32_t offset) {
    unsigned bank=offset/S5L8920_IOP_VIC_STRIDE;
    uint32_t reg=offset%S5L8920_IOP_VIC_STRIDE;
    if(!i || !i->initialized || !i->vic_configured || !i->reset_seen ||
       i->reset_asserted || bank>=S5L8920_IOP_VIC_COUNT || (reg&3u) ||
       reg>=0xfe0u || reg==PL192_PROTECTION || i->vic[bank].protection)return NULL;
    return &i->vic[bank];
}
bool s5l8920_iop_vic_configure(s5l8920_iop_t *i) {
    if(!i || !i->initialized || i->servicing)return false;
    if(i->vic_configured)return true;
    if(i->reset_seen || i->running)return false;
    for(unsigned n=0;n<S5L8920_IOP_VIC_COUNT;++n)pl192_reset(&i->vic[n]);
    i->vic_configured=true;refresh_interrupts(i);return true;
}
bool s5l8920_iop_vic_read(s5l8920_iop_t *i,uint32_t offset,uint32_t *value) {
    pl192_t *v=vic_at(i,offset);
    if(!v || !pl192_read(v,offset%S5L8920_IOP_VIC_STRIDE,false,value))return false;
    refresh_interrupts(i);return true;
}
bool s5l8920_iop_vic_write(s5l8920_iop_t *i,uint32_t offset,uint32_t value) {
    pl192_t *v=vic_at(i,offset);
    if(!v || !pl192_write(v,offset%S5L8920_IOP_VIC_STRIDE,false,value))return false;
    refresh_interrupts(i);return true;
}

static bool bus_failed(void *ctx) {
    const s5l8920_iop_t *i=ctx;
    return !i->servicing || i->failure.reason!=S5L8920_IOP_NO_FAILURE;
}
static void fail(s5l8920_iop_t *i,s5l8920_iop_failure_reason_t reason,
                 uint32_t address,unsigned size,bool write,uint32_t value) {
    if(i->failure.reason)return;
    i->failure=(s5l8920_iop_failure_t){reason,address,i->cpu.r[15],value,size,write};
}
static uint8_t *memory_at(s5l8920_iop_t *i,uint32_t address,unsigned size,
                          bool write,uint32_t value) {
    if(bus_failed(i))return NULL;
    if(!i->running || i->reset_asserted || i->programmed!=7u ||
       size>i->firmware_size || address>i->firmware_size-size) {
        fail(i,S5L8920_IOP_UNMAPPED,address,size,write,value);return NULL;
    }
    /* The complete physical range was checked at RUN, and base+size cannot
     * wrap. Revalidate each access: the supplied RAM capability may change. */
    uint8_t *p=i->memory(i->memory_owner,i->firmware_base+address,size);
    if(!p)fail(i,S5L8920_IOP_MEMORY_UNAVAILABLE,address,size,write,value);
    return p;
}
static uint32_t read_value(s5l8920_iop_t *i,uint32_t address,unsigned size) {
    if(bus_failed(i))return 0;
    if(ap_vic_address(address)) {
        fail(i,i->signal_ap?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,false,0);return 0;
    }
    if(vic_address(address)) {
        uint32_t value;
        if(size==4u && s5l8920_iop_vic_read(i,address-S5L8920_IOP_VIC_BASE,&value))return value;
        fail(i,i->vic_configured?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,false,0);return 0;
    }
    uint8_t *p=memory_at(i,address,size,false,0);if(!p)return 0;
    uint32_t v=0;
    for(unsigned n=0;n<size;++n)v|=(uint32_t)p[n]<<(8u*n);
    return v;
}
static void write_value(s5l8920_iop_t *i,uint32_t address,unsigned size,uint32_t value) {
    if(bus_failed(i))return;
    if(ap_vic_address(address)) {
        uint32_t relative=address-AP_VIC_BASE;
        if(i->signal_ap && size==4u && relative%S5L8920_IOP_VIC_STRIDE==PL192_SOFTINT &&
           i->signal_ap(i->memory_owner,relative/S5L8920_IOP_VIC_STRIDE,value))return;
        fail(i,i->signal_ap?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,true,value);return;
    }
    if(vic_address(address)) {
        if(size==4u && s5l8920_iop_vic_write(i,address-S5L8920_IOP_VIC_BASE,value))return;
        fail(i,i->vic_configured?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,true,value);return;
    }
    uint8_t *p=memory_at(i,address,size,true,value);if(!p)return;
    for(unsigned n=0;n<size;++n)p[n]=(uint8_t)(value>>(8u*n));
    i->bytes_written+=size;
}
#define ACCESSORS(bits) \
static uint##bits##_t read##bits(void *ctx,uint32_t a) { \
    return (uint##bits##_t)read_value(ctx,a,(bits)/8u); \
} \
static void write##bits(void *ctx,uint32_t a,uint##bits##_t v) { \
    write_value(ctx,a,(bits)/8u,v); \
}
ACCESSORS(8)
ACCESSORS(16)
ACCESSORS(32)
#undef ACCESSORS

void s5l8920_iop_init(s5l8920_iop_t *i) {
    if(!i)return;
    memset(i,0,sizeof *i);
    i->bus=(arm_bus_t){.ctx=i,.read8=read8,.read16=read16,.read32=read32,
        .write8=write8,.write16=write16,.write32=write32,.access_failed=bus_failed};
    i->initialized=arm_reset_profile(&i->cpu,&i->bus,ARM_ARCH_V4T);
}
bool s5l8920_iop_reset_line(s5l8920_iop_t *i,bool asserted) {
    if(!i || !i->initialized || i->servicing)return false;
    if(i->reset_asserted==asserted)return true;
    if(asserted) {
        if(!arm_reset_profile(&i->cpu,&i->bus,ARM_ARCH_V4T))return false;
        i->failure=(s5l8920_iop_failure_t){0};
        i->running=false;i->reset_seen=true;i->programmed=0;
        i->firmware_base=i->firmware_mask=i->firmware_size=i->self=0;
        i->bytes_written=0;
        refresh_interrupts(i);
    }
    i->reset_asserted=asserted;
    return true;
}
static bool accessible(const s5l8920_iop_t *i,uint32_t offset) {
    return i && i->initialized && i->reset_seen && !i->reset_asserted &&
        !i->servicing && !(offset&3u);
}
bool s5l8920_iop_read(const s5l8920_iop_t *i,uint32_t offset,uint32_t *value) {
    if(!value || !accessible(i,offset))return false;
    switch(offset) {
    case 0x100:
        /* A capability stop does not clear RUN or assert hardware STOP. */
        *value=i->running?1u:2u;return true;
    case 0x110:if(!(i->programmed&1u))return false;*value=i->firmware_base;return true;
    case 0x114:if(!(i->programmed&2u))return false;*value=i->firmware_mask;return true;
    case 0x118:if(!(i->programmed&4u))return false;*value=i->self;return true;
    default:return false;
    }
}
bool s5l8920_iop_write(s5l8920_iop_t *i,uint32_t offset,uint32_t value,
                       s5l8920_iop_memory_fn memory,void *owner) {
    if(!accessible(i,offset))return false;
    if(offset==0x100u) {
        if(value==0u) { i->running=false;return true; }
        if(value==0x10u)
            return !i->running && !i->programmed && i->cpu.cycles==0u && i->cpu.r[15]==0u;
        if(value!=1u || i->programmed!=7u || i->failure.reason || !memory ||
           i->firmware_base>UINT32_MAX-(i->firmware_size-1u) ||
           !memory(owner,i->firmware_base,i->firmware_size))return false;
        i->running=true;return true;
    }
    if(i->running || i->failure.reason)return false;
    switch(offset) {
    case 0x110:
        if(value&0xfffu)return false;
        i->firmware_base=value;i->programmed|=1u;return true;
    case 0x114: {
        if(value&~0x3ffff000u)return false;
        uint32_t size=(0x3fffffffu^value)+1u;
        if(size<0x1000u || (size&(size-1u)))return false;
        i->firmware_mask=value;i->firmware_size=size;i->programmed|=2u;return true;
    }
    case 0x118:
        if(value!=S5L8920_IOP_BASE)return false;
        i->self=value;i->programmed|=4u;return true;
    default:return false;
    }
}
arm_status_t s5l8920_iop_service(s5l8920_iop_t *i,
                                 s5l8920_iop_memory_fn memory,void *owner) {
    return s5l8920_iop_service_with_signal(i,memory,NULL,owner);
}
arm_status_t s5l8920_iop_service_with_signal(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,void *owner) {
    if(!i || !i->initialized || !i->reset_seen || i->servicing)return ARM_HALT;
    if(!i->running || i->reset_asserted)return ARM_OK;
    if(i->failure.reason || !memory)return ARM_HALT;
    i->memory=memory;i->signal_ap=signal_ap;i->memory_owner=owner;i->servicing=true;
    arm_status_t result=arm_step(&i->cpu);
    i->servicing=false;i->memory=NULL;i->signal_ap=NULL;i->memory_owner=NULL;
    return result;
}
