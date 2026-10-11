/* Independently implemented from the N88 bootloader's register/descriptor
 * accesses and the matching platform's public driver. See docs/ios6.md.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_cdma.h"
#include <string.h>

#define RUNNING UINT32_C(0x10000)
#define DONE UINT32_C(0x80000)
#define EVENTS UINT32_C(0x1c0000)

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void store_word(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(value>>(8u*i));
}
static bool busy(const s5l8920_cdma_t *c) {
    return ((c->channel[0].control|c->channel[1].control)&RUNNING)!=0u;
}
void s5l8920_cdma_init(s5l8920_cdma_t *c) {
    if(c) { memset(c,0,sizeof *c);c->initialized=true; }
}
bool s5l8920_cdma_read(const s5l8920_cdma_t *c,bool aes,uint32_t offset,uint32_t *value) {
    /* AES readback semantics have not been established for this target. */
    if(!c || !c->initialized || !value || aes || (offset&3u))return false;
    unsigned bank=offset/0x1000u;
    if(bank<1u || bank>2u)return false;
    const s5l8920_cdma_channel_t *ch=&c->channel[bank-1u];
    if(!ch->reset)return false;
    switch(offset%0x1000u) {
    case 0: *value=ch->control;return true;
    case 0x0c: if(!ch->size_programmed)return false;*value=ch->remaining;return true;
    case 0x10: if(!ch->started)return false;*value=ch->address;return true;
    case 0x14: if(!ch->descriptor_programmed)return false;*value=ch->descriptor;return true;
    default:return false;
    }
}
static bool aes_write(s5l8920_cdma_t *c,uint32_t offset,uint32_t value) {
    if(busy(c))return false;
    if(offset==0u) {
        /* Channel1, enabled CBC, supplied software key only; sizes0/1/2. */
        if((value&~UINT32_C(0xd0000))!=UINT32_C(0x120100) ||
           ((value>>18)&3u)==3u)return false;
        c->setup=value;c->setup_programmed=true;return true;
    }
    if(offset>=0x10u && offset<0x20u) {
        unsigned i=(offset-0x10u)/4u;store_word(c->iv+4u*i,value);
        c->iv_programmed|=(uint8_t)(1u<<i);return true;
    }
    if(offset>=0x20u && offset<0x40u) {
        unsigned i=(offset-0x20u)/4u;store_word(c->key+4u*i,value);
        c->key_programmed|=(uint8_t)(1u<<i);return true;
    }
    return false;
}
static bool descriptor(s5l8920_cdma_channel_t *ch,unsigned bank,
                       s5l8920_cdma_memory_fn memory,void *owner) {
    if(!memory || !ch->size_programmed || !ch->descriptor_programmed ||
       !ch->remaining || (ch->remaining&15u) || (ch->descriptor&31u))return false;
    const uint8_t *p=memory(owner,ch->descriptor,32u);
    if(!p || word(p+4u)!=(bank==0u?0x30103u:0x103u) || word(p+12u)!=ch->remaining)return false;
    uint32_t terminal=word(p),address=word(p+8u);
    if(terminal&31u)return false;
    const uint8_t *end=memory(owner,terminal,32u);
    if(!end || word(end+4u)!=0u || !memory(owner,address,ch->remaining))return false;
    ch->address=address;ch->terminal=terminal;return true;
}
static bool pair_valid(const s5l8920_cdma_t *c,s5l8920_cdma_memory_fn memory,void *owner) {
    const s5l8920_cdma_channel_t *a=&c->channel[0],*b=&c->channel[1];
    if(a->remaining!=b->remaining)return false;
    uint8_t *src=memory(owner,a->address,a->remaining),*dst=memory(owner,b->address,b->remaining);
    if(!src || !dst)return false;
    /* Compare backing views, including different physical aliases. */
    uintptr_t x=(uintptr_t)src,y=(uintptr_t)dst;
    if(x!=y && (x>y?x-y:y-x)<a->remaining)return false;
    return true;
}
static bool channel_write(s5l8920_cdma_t *c,unsigned bank,uint32_t reg,uint32_t value,
                          s5l8920_cdma_memory_fn memory,void *owner) {
    s5l8920_cdma_channel_t *ch=&c->channel[bank];
    if(reg==0u && value==2u) {
        if(busy(c))return false; /* Live reset/abort coupling remains unknown. */
        memset(ch,0,sizeof *ch);ch->reset=true;return true;
    }
    if(!ch->reset || (ch->control&RUNNING))return false;
    if(reg==0x0cu) { ch->remaining=value;ch->size_programmed=true;return true; }
    if(reg==0x14u) { ch->descriptor=value;ch->descriptor_programmed=true;return true; }
    if(reg!=0u || (value&~(EVENTS|0x181u)))return false;
    uint32_t mode=value&0x180u;
    if(mode!=0u && mode!=(bank==0u?0x180u:0x80u))return false;
    if(value&1u) {
        if(mode!=(bank==0u?0x180u:0x80u) || (ch->control&EVENTS&~value) ||
           !descriptor(ch,bank,memory,owner))return false;
        if(bank==0u) {
            unsigned words=4u+2u*((c->setup>>18)&3u);
            unsigned mask=(1u<<words)-1u;
            if(!c->setup_programmed || c->iv_programmed!=15u ||
               (c->key_programmed&mask)!=mask || !aes_init(&c->cipher,c->key,32u*words))return false;
            memcpy(c->chain,c->iv,16u);
        }
        ch->control=(ch->control&EVENTS&~value)|mode|RUNNING;
        ch->started=true;
        if((c->channel[1u-bank].control&RUNNING) && !pair_valid(c,memory,owner))return false;
    } else ch->control=(ch->control&EVENTS&~value)|mode;
    return true;
}
bool s5l8920_cdma_write(s5l8920_cdma_t *c,bool aes,uint32_t offset,uint32_t value,
                      s5l8920_cdma_memory_fn memory,void *owner) {
    if(!c || !c->initialized || (offset&3u))return false;
    if(aes)return aes_write(c,offset,value);
    unsigned channel=offset/0x1000u;
    if(channel<1u || channel>2u)return false;
    /* A malformed GO must not publish a partial descriptor/context change. */
    s5l8920_cdma_t next=*c;
    if(!channel_write(&next,channel-1u,offset%0x1000u,value,memory,owner))return false;
    *c=next;return true;
}
bool s5l8920_cdma_service(s5l8920_cdma_t *c,s5l8920_cdma_memory_fn memory,void *owner) {
    if(!c || !c->initialized || !memory)return false;
    s5l8920_cdma_channel_t *a=&c->channel[0],*b=&c->channel[1];
    if(!(a->control&RUNNING) || !(b->control&RUNNING))return true;
    if(!pair_valid(c,memory,owner) || a->remaining<16u || (a->remaining&15u))return false;
    uint8_t *src=memory(owner,a->address,16u),*dst=memory(owner,b->address,16u);
    if(!src || !dst)return false;
    uint8_t input[16],output[16];memcpy(input,src,16u);
    if(c->setup&0x10000u) {
        for(unsigned i=0;i<16u;++i)input[i]^=c->chain[i];
        aes_encrypt_block(&c->cipher,input,output);memcpy(c->chain,output,16u);
    } else {
        aes_decrypt_block(&c->cipher,input,output);
        for(unsigned i=0;i<16u;++i)output[i]^=c->chain[i];
        memcpy(c->chain,input,16u);
    }
    memcpy(dst,output,16u);
    a->address+=16u;b->address+=16u;a->remaining-=16u;b->remaining-=16u;
    if(!a->remaining) {
        a->control=(a->control&~RUNNING)|DONE;b->control=(b->control&~RUNNING)|DONE;
        a->descriptor=a->terminal;b->descriptor=b->terminal;
        memset(&c->cipher,0,sizeof c->cipher);memset(c->chain,0,sizeof c->chain);
    }
    return true;
}
