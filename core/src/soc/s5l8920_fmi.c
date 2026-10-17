/* Partial S5L8920 polled NAND controller.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_fmi.h"
#include <string.h>

enum { TIMING=1u, COMMAND=2u, ADDRESS=4u, ADDRESS_END=8u, PIO=16u };
enum { IDLE=0u, RESET=1u, READ_ID=2u, BYTE=3u };

bool s5l8920_fmi_init(s5l8920_fmi_t *f) {
    if(!f || f->servicing)return false;
    memset(f,0,sizeof *f);f->initialized=true;return true;
}
bool s5l8920_fmi_reset_line(s5l8920_fmi_t *f,bool asserted) {
    if(!f || !f->initialized || f->servicing)return false;
    if(asserted && !f->reset_asserted) {
        uint64_t commands=f->commands,bytes=f->bytes_latched;
        (void)s5l8920_fmi_init(f);f->commands=commands;f->bytes_latched=bytes;
    }
    f->reset_asserted=asserted;return true;
}
static bool usable(const s5l8920_fmi_t *f) {
    return f && f->initialized && !f->reset_asserted && !f->servicing;
}
bool s5l8920_fmi_read(const s5l8920_fmi_t *f,uint32_t offset,uint32_t *value) {
    if(!usable(f) || !f->enabled || !value)return false;
    switch(offset) {
    case 0x408u:
        if(!(f->programmed&TIMING))return false;
        *value=f->timing;return true;
    case 0x40cu:*value=f->selected;return true;
    case 0x444u:*value=f->status;return true;
    case 0x448u:
        if(f->control!=0x50u || !f->data_valid)return false;
        *value=f->data;return true;
    default:return false;
    }
}
bool s5l8920_fmi_write(s5l8920_fmi_t *f,uint32_t offset,uint32_t value) {
    if(!usable(f))return false;
    if(offset==0x400u) {
        if(value!=1u || f->pending || f->control==0x50u)return false;
        f->enabled=true;return true;
    }
    if(offset==0x408u) {
        if(f->pending || f->control==0x50u)return false;
        f->timing=value;f->programmed|=TIMING;return true;
    }
    if(!f->enabled)return false;
    if(offset==0x444u) {
        if(value&~9u)return false;
        f->status&=~value;return true;
    }
    if(f->pending)return false;
    if(offset==0x410u) {
        if(!value) {
            f->control=0u;f->data_valid=false;return true;
        }
        if(f->control==0x50u)return false;
        if(value==0x50u) {
            if(!f->id_valid || f->cursor>=S5L8920_FMI_ID_SIZE ||
               (f->programmed&(TIMING|PIO))!=(TIMING|PIO) || f->timing ||
               f->bytes_latched==UINT64_MAX)return false;
            f->control=value;f->data_valid=false;f->pending=BYTE;return true;
        }
        if((value!=1u && value!=9u) || !(f->programmed&COMMAND) ||
           f->status || f->commands==UINT64_MAX)return false;
        if(value==1u) {
            if(f->command!=0xffu)return false;
        }else if(f->command!=0x90u ||
                 (f->programmed&(ADDRESS|ADDRESS_END))!=(ADDRESS|ADDRESS_END))return false;
        f->control=value;f->pending=value==1u?RESET:READ_ID;
        f->id_valid=f->data_valid=false;f->cursor=0u;
        return true;
    }
    if(f->control==0x50u)return false;
    switch(offset) {
    case 0x40cu:
        if(value>0xffu || (value && (value&(value-1u))))return false;
        if(f->selected!=value) {
            f->selected=(uint8_t)value;f->id_valid=f->data_valid=false;f->cursor=0u;
        }
        return true;
    case 0x414u:
        if(value!=0x90u && value!=0xffu)return false;
        f->command=(uint8_t)value;f->programmed|=COMMAND;return true;
    case 0x418u:
        if(value>0xffu)return false;
        f->address=(uint8_t)value;f->programmed|=ADDRESS;return true;
    case 0x420u:
        if(value)return false;
        f->programmed|=ADDRESS_END;return true;
    case 0x424u:
        if(value)return false;
        f->programmed|=PIO;return true;
    default:return false;
    }
}
bool s5l8920_fmi_service(s5l8920_fmi_t *f,s5l8920_fmi_device_fn device,void *owner) {
    if(!f || !f->initialized || f->servicing)return false;
    if(f->reset_asserted || !f->pending)return true;
    if(!f->enabled)return false;
    if(f->pending==BYTE) {
        if(!f->id_valid || f->cursor>=S5L8920_FMI_ID_SIZE || f->bytes_latched==UINT64_MAX)return false;
        f->data=f->id[f->cursor++];f->data_valid=true;f->bytes_latched++;f->pending=IDLE;return true;
    }
    if(!device || f->commands==UINT64_MAX)return false;
    unsigned chip=S5L8920_FMI_NO_CHIP;
    if(f->selected) { chip=0u;while(!(f->selected&(1u<<chip)))chip++; }
    uint8_t id[S5L8920_FMI_ID_SIZE]={0};
    f->servicing=true;
    bool okay=device(owner,chip,f->command,f->command==0x90u?f->address:0u,id);
    f->servicing=false;
    if(!okay)return false;
    if(f->pending==READ_ID) {
        memcpy(f->id,id,sizeof id);f->id_valid=true;f->status|=9u;
    }else f->status|=1u;
    f->commands++;f->pending=IDLE;return true;
}
