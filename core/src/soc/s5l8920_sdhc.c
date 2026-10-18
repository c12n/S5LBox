/* SDHCI reset subset; no card responses or implicit clock stabilization.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_sdhc.h"

bool s5l8920_sdhc_init(s5l8920_sdhc_t *s) {
    if(!s)return false;
    if(!s->initialized)*s=(s5l8920_sdhc_t){.initialized=true};
    return true;
}
static unsigned width(uint32_t offset) {
    switch(offset) {
    case 0x00:case 0x08:return 4;
    case 0x04:case 0x06:case 0x0c:case 0x2c:case 0x30:case 0x32:
    case 0x34:case 0x36:case 0x38:case 0x3a:return 2;
    case 0x28:case 0x2e:case 0x2f:return 1;
    default:return 0;
    }
}
bool s5l8920_sdhc_read(const s5l8920_sdhc_t *s,uint32_t offset,unsigned size,uint32_t *value) {
    if(!s || !s->initialized || !value || !size || size!=width(offset))return false;
    switch(offset) {
    case 0x00:*value=s->dma_address;break;
    case 0x04:*value=s->block_size;break;
    case 0x06:*value=s->block_count;break;
    case 0x08:*value=s->argument;break;
    case 0x0c:*value=s->transfer_mode;break;
    case 0x28:*value=s->host_control;break;
    case 0x2c:*value=s->clock_control|(s->clock_stable?2u:0u);break;
    case 0x2e:*value=s->timeout;break;
    case 0x2f:*value=s->reset_pending;break;
    case 0x30:*value=s->normal_status|(s->error_status?0x8000u:0u);break;
    case 0x32:*value=s->error_status;break;
    case 0x34:*value=s->normal_enable;break;
    case 0x36:*value=s->error_enable;break;
    case 0x38:*value=s->normal_signal;break;
    case 0x3a:*value=s->error_signal;break;
    default:return false;
    }
    return true;
}
bool s5l8920_sdhc_write(s5l8920_sdhc_t *s,uint32_t offset,unsigned size,uint32_t value) {
    if(!s || !s->initialized || !size || size!=width(offset) ||
       (size<4u && value>>(8u*size)))return false;
    if(offset==0x2fu) {
        if(value&~7u)return false;
        if(!value)return true;
        if(s->reset_pending)return value==s->reset_pending;
        s->reset_pending=(uint8_t)value;return true;
    }
    if(s->reset_pending)return false;
    switch(offset) {
    case 0x00:s->dma_address=value;break;
    case 0x04:
        if(value&0x8000u)return false;
        s->block_size=(uint16_t)value;break;
    case 0x06:s->block_count=(uint16_t)value;break;
    case 0x08:s->argument=value;break;
    case 0x0c:
        if(value&~0x37u)return false;
        s->transfer_mode=(uint16_t)value;break;
    case 0x28:
        /* LED and four-bit bus selection only; no unverified DMA/high-speed mode. */
        if(value&~3u)return false;
        s->host_control=(uint8_t)value;break;
    case 0x2c: {
        unsigned divider=value>>8;
        if((value&~0xff07u) || (divider && (divider&(divider-1u))))return false;
        value&=~2u; /* Hardware status is not writable, including through RMW. */
        if((s->clock_control^value)&0xff01u)s->clock_stable=false;
        s->clock_control=(uint16_t)value;break;
    }
    case 0x2e:
        if(value>14u)return false;
        s->timeout=(uint8_t)value;break;
    case 0x30:
        if(value&~0x81ffu)return false;
        s->normal_status&=(uint16_t)~(value&0x1ffu);break;
    case 0x32:
        if(value&~0x1ffu)return false;
        s->error_status&=(uint16_t)~value;break;
    case 0x34:case 0x38:
        if(value&~0x1ffu)return false;
        if(offset==0x34u)s->normal_enable=(uint16_t)value;
        else s->normal_signal=(uint16_t)value;
        break;
    case 0x36:case 0x3a:
        if(value&~0x1ffu)return false;
        if(offset==0x36u)s->error_enable=(uint16_t)value;
        else s->error_signal=(uint16_t)value;
        break;
    default:return false;
    }
    return true;
}
bool s5l8920_sdhc_service(s5l8920_sdhc_t *s,bool clock_available) {
    if(!s || !s->initialized)return false;
    if(!clock_available || !s->reset_pending)return true;
    if(s->reset_pending&1u)*s=(s5l8920_sdhc_t){.initialized=true};
    else {
        if(s->reset_pending&2u)s->normal_status&=(uint16_t)~1u;
        if(s->reset_pending&4u)s->normal_status&=(uint16_t)~0x3eu;
        s->reset_pending=0;
    }
    return true;
}
bool s5l8920_sdhc_clock_service(s5l8920_sdhc_t *s,bool source_available) {
    if(!s || !s->initialized)return false;
    if(!s->reset_pending)s->clock_stable=source_available && (s->clock_control&1u);
    return true;
}
