/* Original AppleARM7M version1 control sequence, backed by an actual CPU.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_iop.h"
#include "s5l8920_fmi.h"
#include "s5l8920_sdhc.h"
#include <string.h>

#define AP_VIC_BASE UINT32_C(0xbf200000)
static bool gate_address(uint32_t address) {
    return address>=S5L8920_CLOCK_GATE_BASE &&
        address-S5L8920_CLOCK_GATE_BASE<4u*S5L8920_CLOCK_GATE_COUNT;
}
static bool ap_vic_address(uint32_t address) {
    return address>=AP_VIC_BASE &&
        (address-AP_VIC_BASE)/S5L8920_IOP_VIC_STRIDE<S5L8920_IOP_VIC_COUNT &&
        (address-AP_VIC_BASE)%S5L8920_IOP_VIC_STRIDE<0x1000u;
}
static bool fmi_address(uint32_t address) {
    return address>=S5L8920_FMI_BASE &&
        (address-S5L8920_FMI_BASE)/S5L8920_FMI_STRIDE<S5L8920_FMI_COUNT &&
        (address-S5L8920_FMI_BASE)%S5L8920_FMI_STRIDE<0x1000u;
}

static void refresh_interrupts(s5l8920_iop_t *i) {
    if(!i->vic_configured)return;
    if(i->timer.configured)(void)pl192_set_line(&i->vic[0],5,i->timer.pending);
    bool irq=false,fiq=false;
    for(unsigned n=0;n<S5L8920_IOP_VIC_COUNT;++n) {
        irq=irq || pl192_irq(&i->vic[n]);fiq=fiq || pl192_fiq(&i->vic[n]);
    }
    i->cpu.irq_line=irq;i->cpu.fiq_line=fiq;
}
bool s5l8920_iop_timer_configure(s5l8920_iop_t *i) {
    if(!i || !i->initialized || i->servicing || !i->vic_configured)return false;
    if(i->timer.configured)return true;
    if(i->reset_seen || i->running)return false;
    i->timer=(s5l8920_iop_timer_t){.configured=true};
    refresh_interrupts(i);return true;
}
bool s5l8920_iop_timer_clock(s5l8920_iop_t *i,uint64_t ticks) {
    if(!i || !i->initialized || i->servicing || !i->timer.configured ||
       !i->vic_configured)return false;
    s5l8920_iop_timer_t *t=&i->timer;
    if(ticks && t->enabled && t->programmed && !t->expired) {
        if(ticks>=t->remaining) {t->remaining=0;t->expired=true;t->pending=true;}
        else t->remaining-=(uint32_t)ticks;
    }
    refresh_interrupts(i);return true;
}
static bool timer_address(uint32_t address) {
    return address>=UINT32_C(0xbf100200) && address<UINT32_C(0xbf100228);
}
static bool timer_write(s5l8920_iop_t *i,uint32_t address,uint32_t value) {
    s5l8920_iop_timer_t *t=&i->timer;
    if(!t->configured)return false;
    if(address==UINT32_C(0xbf10020c)) {
        t->remaining=value;t->programmed=true;t->expired=false;
    } else if(address==UINT32_C(0xbf100224) && !(value&~3u) &&
              (!(value&1u) || t->programmed)) {
        t->enabled=(value&1u)!=0u;
        if(value&2u)t->pending=false;
    } else return false;
    refresh_interrupts(i);return true;
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
static bool ram_range(s5l8920_iop_t *i,uint32_t address,unsigned size,
                      bool write,uint32_t value) {
    uint32_t limit=address<i->firmware_size?i->firmware_size:i->ram_size;
    if(!i->running || i->reset_asserted || i->programmed!=7u ||
       !size || size>limit || address>limit-size) {
        fail(i,S5L8920_IOP_UNMAPPED,address,size,write,value);return false;
    }
    return true;
}
static uint8_t *memory_at(s5l8920_iop_t *i,uint32_t address,unsigned size,
                          bool write,uint32_t value) {
    if(bus_failed(i))return NULL;
    if(!ram_range(i,address,size,write,value))return NULL;
    /* Firmware bounds were checked at RUN; the optional aperture is bounded
     * by configuration. Revalidate backing on every access, including aliases. */
    uint32_t base=address<i->firmware_size?i->firmware_base:S5L8920_IOP_RAM_BASE;
    uint8_t *p=i->memory(i->memory_owner,base+address,size);
    if(!p)fail(i,S5L8920_IOP_MEMORY_UNAVAILABLE,address,size,write,value);
    return p;
}
bool s5l8920_iop_ram_configure(s5l8920_iop_t *i,uint32_t size) {
    if(!i || !i->initialized || i->servicing || !size || (size&0xfffu) ||
       size>UINT32_C(0x40000000))return false;
    if(i->ram_size)return i->ram_size==size;
    if(i->reset_seen || i->running)return false;
    i->ram_size=size;return true;
}
bool s5l8920_iop_cache_configure(s5l8920_iop_t *i,
    s5l8920_iop_cache_line_t *lines,size_t capacity) {
    if(!i || !i->initialized || i->servicing || !lines || !capacity ||
       capacity>SIZE_MAX/sizeof *lines)return false;
    if(i->cache_lines)return i->cache_lines==lines && i->cache_capacity==capacity;
    if(i->reset_seen || i->running)return false;
    /* Tags remain unavailable until the original initialization command. */
    i->cache_lines=lines;i->cache_capacity=capacity;return true;
}
static bool cache_address(uint32_t address) {
    return address>=S5L8920_IOP_BASE && address-S5L8920_IOP_BASE<0x100u;
}
static s5l8920_iop_cache_line_t *cache_line(s5l8920_iop_t *i,uint32_t address) {
    uint32_t tag=address&~15u;
    s5l8920_iop_cache_line_t *empty=NULL;
    for(size_t n=0;n<i->cache_capacity;++n) {
        s5l8920_iop_cache_line_t *line=&i->cache_lines[n];
        if(line->valid && line->address==tag)return line;
        if(!line->valid && !empty)empty=line;
    }
    if(!empty) {fail(i,S5L8920_IOP_CACHE_LIMIT,tag,16,false,0);return NULL;}
    uint8_t *p=memory_at(i,tag,16,false,0);if(!p)return NULL;
    memcpy(empty->data,p,16);empty->address=tag;empty->dirty=false;empty->valid=true;
    return empty;
}
static bool cache_parts(s5l8920_iop_t *i,uint32_t address,unsigned size,
                         bool write,uint32_t value,uint8_t *parts[4],
                         s5l8920_iop_cache_line_t *lines[4]) {
    if(!ram_range(i,address,size,write,value))return false;
    s5l8920_iop_cache_line_t *line=NULL;
    for(unsigned n=0;n<size;++n) {
        uint32_t a=address+n;
        if(!line || line->address!=(a&~15u))line=cache_line(i,a);
        if(!line)return false;
        parts[n]=line->data+(a&15u);lines[n]=line;
    }
    return true;
}
static bool clean_line(s5l8920_iop_t *i,s5l8920_iop_cache_line_t *line) {
    if(!line->valid || !line->dirty)return true;
    uint8_t *p=memory_at(i,line->address,16,true,0);if(!p)return false;
    memcpy(p,line->data,16);line->dirty=false;i->bytes_written+=16;return true;
}
static bool cache_command(s5l8920_iop_t *i,uint32_t offset,uint32_t value) {
    if(!i->cache_lines)return false;
    if(offset==0x20u && value==1u) {
        if(i->cache_ready || i->cache_enabled)return false;
        memset(i->cache_lines,0,i->cache_capacity*sizeof *i->cache_lines);
        i->cache_ready=true;return true;
    }
    if(!i->cache_ready)return false;
    if(offset==0u) {
        if(value!=0u && value!=4u)return false;
        i->cache_enabled=value==4u;return true;
    }
    if(offset==0x18u) {
        uint32_t limit=value<i->firmware_size?i->firmware_size:i->ram_size;
        if((value&15u) || limit<16u || value>limit-16u)return false;
        i->cache_address=value;i->cache_address_valid=true;return true;
    }
    bool all=offset==0x0cu || offset==0x20u;
    bool clean=offset==0x0cu || offset==0x1cu;
    if(value || (!all && offset!=0x1cu && offset!=0x24u) ||
       (!all && !i->cache_address_valid))return false;
    for(size_t n=0;n<i->cache_capacity;++n) {
        s5l8920_iop_cache_line_t *line=&i->cache_lines[n];
        if(!line->valid || (!all && line->address!=i->cache_address))continue;
        if(clean) {if(!clean_line(i,line))return false;}
        else {line->valid=false;line->dirty=false;}
    }
    return true;
}
static uint32_t read_value(s5l8920_iop_t *i,uint32_t address,unsigned size) {
    if(bus_failed(i))return 0;
    if(gate_address(address)) {
        uint32_t value=0;
        if(i->gate && size==4u && !(address&3u) &&
           i->gate(i->memory_owner,(address-S5L8920_CLOCK_GATE_BASE)/4u,false,&value))return value;
        fail(i,i->gate?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,false,0);return 0;
    }
    if(address>=S5L8920_SDHC_BASE && address-S5L8920_SDHC_BASE<S5L8920_SDHC_SIZE) {
        uint32_t offset=address-S5L8920_SDHC_BASE,value=0;
        if(i->sdhc && !(address&(size-1u)) && offset<=S5L8920_SDHC_SIZE-size &&
           i->sdhc(i->memory_owner,offset,size,false,&value))return value;
        fail(i,i->sdhc?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,false,0);return 0;
    }
    if(fmi_address(address)) {
        uint32_t relative=address-S5L8920_FMI_BASE,value=0;
        if(i->fmi && size==4u && !(address&3u) &&
           i->fmi(i->memory_owner,relative/S5L8920_FMI_STRIDE,
                  relative%S5L8920_FMI_STRIDE,false,&value))return value;
        fail(i,i->fmi?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,false,0);return 0;
    }
    if(timer_address(address)) {
        if(i->timer.configured && i->timebase && size==4u &&
           (address==UINT32_C(0xbf100200) || address==UINT32_C(0xbf100204))) {
            uint64_t ticks=i->timebase(i->memory_owner);
            return address==UINT32_C(0xbf100200)?(uint32_t)ticks:(uint32_t)(ticks>>32);
        }
        fail(i,i->timer.configured?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,false,0);return 0;
    }
    if(cache_address(address)) {
        fail(i,i->cache_lines?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,false,0);return 0;
    }
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
    uint32_t v=0;
    if(i->cache_enabled) {
        uint8_t *parts[4];s5l8920_iop_cache_line_t *lines[4];
        if(!cache_parts(i,address,size,false,0,parts,lines))return 0;
        for(unsigned n=0;n<size;++n)v|=(uint32_t)*parts[n]<<(8u*n);
    } else {
        uint8_t *p=memory_at(i,address,size,false,0);if(!p)return 0;
        for(unsigned n=0;n<size;++n)v|=(uint32_t)p[n]<<(8u*n);
    }
    return v;
}
static void write_value(s5l8920_iop_t *i,uint32_t address,unsigned size,uint32_t value) {
    if(bus_failed(i))return;
    if(gate_address(address)) {
        uint32_t operand=value;
        if(i->gate && size==4u && !(address&3u) &&
           i->gate(i->memory_owner,(address-S5L8920_CLOCK_GATE_BASE)/4u,true,&operand))return;
        fail(i,i->gate?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,true,value);return;
    }
    if(address>=S5L8920_SDHC_BASE && address-S5L8920_SDHC_BASE<S5L8920_SDHC_SIZE) {
        uint32_t offset=address-S5L8920_SDHC_BASE,operand=value;
        if(i->sdhc && !(address&(size-1u)) && offset<=S5L8920_SDHC_SIZE-size &&
           i->sdhc(i->memory_owner,offset,size,true,&operand))return;
        fail(i,i->sdhc?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,true,value);return;
    }
    if(fmi_address(address)) {
        uint32_t relative=address-S5L8920_FMI_BASE,operand=value;
        if(i->fmi && size==4u && !(address&3u) &&
           i->fmi(i->memory_owner,relative/S5L8920_FMI_STRIDE,
                  relative%S5L8920_FMI_STRIDE,true,&operand))return;
        fail(i,i->fmi?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,true,value);return;
    }
    if(timer_address(address)) {
        if(size==4u && timer_write(i,address,value))return;
        fail(i,i->timer.configured?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,true,value);return;
    }
    if(cache_address(address)) {
        if(size==4u && !(address&3u) && cache_command(i,address-S5L8920_IOP_BASE,value))return;
        fail(i,i->cache_lines?S5L8920_IOP_REGISTER_REFUSED:S5L8920_IOP_UNMAPPED,
             address,size,true,value);return;
    }
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
    if(i->cache_enabled) {
        uint8_t *parts[4];s5l8920_iop_cache_line_t *lines[4];
        if(!cache_parts(i,address,size,true,value,parts,lines))return;
        /* Resolve both granules before modifying a split access. Fills may
         * survive a failed access, but an incomplete store writes no bytes. */
        for(unsigned n=0;n<size;++n) {*parts[n]=(uint8_t)(value>>(8u*n));lines[n]->dirty=true;}
    } else {
        uint8_t *p=memory_at(i,address,size,true,value);if(!p)return;
        for(unsigned n=0;n<size;++n)p[n]=(uint8_t)(value>>(8u*n));
        i->bytes_written+=size;
    }
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
        if(i->cache_ready)return false;
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
    if(i->running || i->failure.reason || i->cache_ready)return false;
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
    return s5l8920_iop_service_with_timebase(i,memory,signal_ap,NULL,owner);
}
arm_status_t s5l8920_iop_service_with_timebase(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,void *owner) {
    return s5l8920_iop_service_with_fmi(i,memory,signal_ap,timebase,NULL,owner);
}
arm_status_t s5l8920_iop_service_with_fmi(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,s5l8920_iop_fmi_fn fmi,void *owner) {
    return s5l8920_iop_service_with_sdhc(i,memory,signal_ap,timebase,fmi,NULL,owner);
}
arm_status_t s5l8920_iop_service_with_sdhc(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,s5l8920_iop_fmi_fn fmi,
    s5l8920_iop_sdhc_fn sdhc,void *owner) {
    return s5l8920_iop_service_with_gates(i,memory,signal_ap,timebase,fmi,sdhc,NULL,owner);
}
arm_status_t s5l8920_iop_service_with_gates(s5l8920_iop_t *i,
    s5l8920_iop_memory_fn memory,s5l8920_iop_signal_fn signal_ap,
    s5l8920_iop_timebase_fn timebase,s5l8920_iop_fmi_fn fmi,
    s5l8920_iop_sdhc_fn sdhc,s5l8920_iop_gate_fn gate,void *owner) {
    if(!i || !i->initialized || !i->reset_seen || i->servicing)return ARM_HALT;
    if(!i->running || i->reset_asserted)return ARM_OK;
    if(i->failure.reason || !memory)return ARM_HALT;
    i->memory=memory;i->signal_ap=signal_ap;i->timebase=timebase;i->fmi=fmi;
    i->sdhc=sdhc;i->gate=gate;i->memory_owner=owner;i->servicing=true;
    arm_status_t result=arm_step(&i->cpu);
    i->servicing=false;i->memory=NULL;i->signal_ap=NULL;i->timebase=NULL;
    i->fmi=NULL;i->sdhc=NULL;i->gate=NULL;i->memory_owner=NULL;
    return result;
}
