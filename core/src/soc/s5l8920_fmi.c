/* Partial S5L8920 polled NAND controller.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_fmi.h"
#include <string.h>

enum { TIMING=1u, COMMAND=2u, ADDRESS=4u, ADDRESS_END=8u, PIO=16u, ECC_MASK=32u, ADDRESS_HIGH=64u,
       RBB=128u, STATUS_MASK=256u, INTERRUPT_ENABLE=512u };
enum { IDLE=0u, RESET=1u, READ_ID=2u, BYTE=3u, READ_PAGE=4u,
       STATUS_COMMAND=5u, STATUS_POLL=6u, READ_MODE=7u };

bool s5l8920_fmi_init(s5l8920_fmi_t *f) {
    if(!f || f->servicing)return false;
    memset(f,0,sizeof *f);f->initialized=true;return true;
}
bool s5l8920_fmi_reset_line(s5l8920_fmi_t *f,bool asserted) {
    if(!f || !f->initialized || f->servicing)return false;
    if(asserted && !f->reset_asserted) {
        uint64_t commands=f->commands,bytes=f->bytes_latched,samples=f->status_samples;
        (void)s5l8920_fmi_init(f);f->commands=commands;f->bytes_latched=bytes;
        f->status_samples=samples;
    }
    f->reset_asserted=asserted;return true;
}
static bool usable(const s5l8920_fmi_t *f) {
    return f && f->initialized && !f->reset_asserted && !f->servicing;
}
static unsigned selected_chip(const s5l8920_fmi_t *f) {
    if(!f->selected)return S5L8920_FMI_NO_CHIP;
    unsigned chip=0u;while(!(f->selected&(1u<<chip)))chip++;
    return chip;
}
bool s5l8920_fmi_read(const s5l8920_fmi_t *f,uint32_t offset,uint32_t *value) {
    if(!usable(f) || !f->enabled || !value)return false;
    switch(offset) {
    case 0x10u:
        if(!(f->programmed&INTERRUPT_ENABLE))return false;
        *value=f->interrupt_enable;return true;
    case 0x408u:
        if(!(f->programmed&TIMING))return false;
        *value=f->timing;return true;
    case 0x40cu:*value=f->selected;return true;
    case 0x418u:
        if(!(f->programmed&ADDRESS))return false;
        *value=f->address;return true;
    case 0x41cu:
        if(!(f->programmed&ADDRESS_HIGH))return false;
        *value=f->address_high;return true;
    case 0x420u:
        if(!(f->programmed&ADDRESS_END))return false;
        *value=f->address_end;return true;
    case 0x440u:
        if(!(f->programmed&STATUS_MASK))return false;
        *value=f->status_interrupt_mask;return true;
    case 0x444u:*value=f->status;return true;
    case 0x448u:
        if(f->control!=0x50u || !f->data_valid)return false;
        *value=f->data;return true;
    case 0x44cu:
        if(!(f->programmed&RBB))return false;
        *value=f->rbb_config;return true;
    case 0x814u:
        if(!(f->programmed&ECC_MASK))return false;
        *value=f->ecc_mask;return true;
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
        if((value&~0x2bu) && value!=0x31ffffu)return false;
        f->status&=~value;return true;
    }
    if(f->pending && !(f->pending==STATUS_POLL && offset==0x410u && !value))return false;
    if(offset==0x410u) {
        if(!value) {
            f->control=0u;f->data_valid=false;f->pending=IDLE;return true;
        }
        if(f->control==0x50u)return false;
        if(value==0x50u) {
            if(f->status_mode) {
                const unsigned required=TIMING|PIO|RBB|STATUS_MASK|INTERRUPT_ENABLE;
                if(!f->selected || (f->programmed&required)!=required ||
                   (f->timing&0x100000u) || f->status || f->status_samples==UINT64_MAX)return false;
                f->control=value;f->data_valid=false;f->pending=STATUS_POLL;return true;
            }
            if(!f->id_valid || f->cursor>=S5L8920_FMI_ID_SIZE ||
               (f->programmed&(TIMING|PIO))!=(TIMING|PIO) || f->timing ||
               f->bytes_latched==UINT64_MAX)return false;
            f->control=value;f->data_valid=false;f->pending=BYTE;return true;
        }
        if((value!=1u && value!=9u && value!=0xbu) || !(f->programmed&COMMAND) ||
           (f->status&0xbu) || f->commands==UINT64_MAX)return false;
        if(value==1u) {
            if(f->command==0x70u || f->command==0u) {
                if(!f->selected || !(f->programmed&TIMING))return false;
                if(!f->command && (!f->status_mode || !f->page_issued))return false;
            }else if(f->command!=0xffu)return false;
        }else if(value==9u) {
            if(f->command!=0x90u || f->address>0xffu || f->address_end ||
               (f->programmed&(ADDRESS|ADDRESS_END))!=(ADDRESS|ADDRESS_END))return false;
        }else if(f->command!=0x3000u || !f->selected || (f->address&0xffffu) ||
                 f->address_end!=4u ||
                 (f->programmed&(TIMING|ADDRESS|ADDRESS_HIGH|ADDRESS_END))!=
                 (TIMING|ADDRESS|ADDRESS_HIGH|ADDRESS_END))return false;
        f->control=value;f->pending=value==1u?
            (f->command==0xffu?RESET:f->command==0x70u?STATUS_COMMAND:READ_MODE):value==9u?READ_ID:READ_PAGE;
        f->id_valid=f->data_valid=false;f->cursor=0u;
        return true;
    }
    if(f->control==0x50u)return false;
    switch(offset) {
    case 0xcu:
        if(value!=0xfu || (f->programmed&(STATUS_MASK|INTERRUPT_ENABLE))!=(STATUS_MASK|INTERRUPT_ENABLE))return false;
        f->interrupt_pending&=~value;return true;
    case 0x10u:
        if(value)return false;
        f->interrupt_enable=value;f->programmed|=INTERRUPT_ENABLE;return true;
    case 0x40cu:
        if(value>0xffu || (value && (value&(value-1u))))return false;
        if(f->selected!=value) {
            f->selected=(uint8_t)value;f->id_valid=f->data_valid=false;f->cursor=0u;
            f->status_mode=f->page_issued=false;
        }
        return true;
    case 0x414u:
        if(value!=0x90u && value!=0xffu && value!=0x3000u && value!=0x70u && value!=0u)return false;
        f->command=value;f->programmed|=COMMAND;return true;
    case 0x418u:
        if(value>0xffu && (value&0xffffu))return false;
        f->address=value;f->programmed|=ADDRESS;return true;
    case 0x41cu:
        if(value>0xffu)return false;
        f->address_high=value;f->programmed|=ADDRESS_HIGH;return true;
    case 0x420u:
        if(value!=0u && value!=4u)return false;
        f->address_end=value;f->programmed|=ADDRESS_END;return true;
    case 0x424u:
        if(value)return false;
        f->programmed|=PIO;return true;
    case 0x440u:
        if(value)return false;
        f->status_interrupt_mask=value;f->programmed|=STATUS_MASK;return true;
    case 0x44cu:
        if(value!=0x4040u)return false;
        f->rbb_config=value;f->programmed|=RBB;return true;
    case 0x814u:
        if(value)return false;
        f->ecc_mask=value;f->programmed|=ECC_MASK;return true;
    default:return false;
    }
}
bool s5l8920_fmi_service_io(s5l8920_fmi_t *f,const s5l8920_fmi_endpoint_t *endpoint,
                           const s5l8920_fmi_status_device_t *status,void *owner) {
    if(!f || !f->initialized || f->servicing)return false;
    if(f->reset_asserted || !f->pending)return true;
    if(!f->enabled)return false;
    if(f->pending==STATUS_POLL) {
        if(!status || !status->sample || f->status_samples==UINT64_MAX)return false;
        uint8_t value=0;f->servicing=true;
        bool okay=status->sample(owner,selected_chip(f),&value);
        f->servicing=false;if(!okay)return false;
        f->data=value;f->data_valid=true;f->status_samples++;
        if((value&(f->rbb_config&255u))==(f->rbb_config>>8)) {
            f->status|=0x20u;f->pending=IDLE;
        }
        return true;
    }
    if(f->pending==BYTE) {
        if(!f->id_valid || f->cursor>=S5L8920_FMI_ID_SIZE || f->bytes_latched==UINT64_MAX)return false;
        f->data=f->id[f->cursor++];f->data_valid=true;f->bytes_latched++;f->pending=IDLE;return true;
    }
    if(f->commands==UINT64_MAX)return false;
    bool mode_command=f->pending==STATUS_COMMAND || f->pending==READ_MODE;
    if(mode_command) {
        if(!status || !status->command)return false;
    }else if(!endpoint || (f->pending==READ_PAGE ? !endpoint->read_page : !endpoint->command))return false;
    unsigned chip=selected_chip(f);
    uint8_t id[S5L8920_FMI_ID_SIZE]={0};
    f->servicing=true;
    bool okay=mode_command?status->command(owner,chip,(uint8_t)f->command):f->pending==READ_PAGE?
        endpoint->read_page(owner,chip,(f->address_high<<16)|(f->address>>16)):
        endpoint->command(owner,chip,(uint8_t)f->command,f->command==0x90u?(uint8_t)f->address:0u,id);
    f->servicing=false;
    if(!okay)return false;
    if(f->pending==STATUS_COMMAND)f->status_mode=true;
    else if(f->pending==READ_MODE)f->status_mode=false;
    else {
        f->status_mode=false;f->page_issued=f->pending==READ_PAGE;
    }
    if(f->pending==READ_ID) {
        memcpy(f->id,id,sizeof id);f->id_valid=true;f->status|=9u;
    }else f->status|=f->pending==READ_PAGE?0xbu:1u;
    f->commands++;f->pending=IDLE;return true;
}
bool s5l8920_fmi_service_endpoint(s5l8920_fmi_t *f,
                                 const s5l8920_fmi_endpoint_t *endpoint,void *owner) {
    return s5l8920_fmi_service_io(f,endpoint,NULL,owner);
}
bool s5l8920_fmi_service(s5l8920_fmi_t *f,s5l8920_fmi_device_fn device,void *owner) {
    const s5l8920_fmi_endpoint_t endpoint={device,NULL};
    return s5l8920_fmi_service_endpoint(f,&endpoint,owner);
}
