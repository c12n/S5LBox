/* Original AppleARM7M version1 control sequence, backed by an actual CPU.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_iop.h"
#include <string.h>

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
    uint8_t *p=memory_at(i,address,size,false,0);if(!p)return 0;
    uint32_t v=0;
    for(unsigned n=0;n<size;++n)v|=(uint32_t)p[n]<<(8u*n);
    return v;
}
static void write_value(s5l8920_iop_t *i,uint32_t address,unsigned size,uint32_t value) {
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
    if(!i || !i->initialized || !i->reset_seen || i->servicing)return ARM_HALT;
    if(!i->running || i->reset_asserted)return ARM_OK;
    if(i->failure.reason || !memory)return ARM_HALT;
    i->memory=memory;i->memory_owner=owner;i->servicing=true;
    arm_status_t result=arm_step(&i->cpu);
    i->servicing=false;i->memory=NULL;i->memory_owner=NULL;
    return result;
}
