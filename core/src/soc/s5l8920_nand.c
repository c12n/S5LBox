/* Optional read-only raw-page backing for the S5L8920 NAND bus.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_nand.h"
#include <string.h>

enum { IDLE, RESET, READ };
static bool span(const void *p,size_t size) {
    return p && size && size-1u<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x<an:x-y<bn;
}
static s5l8920_nand_chip_t *selected(s5l8920_nand_t *n,unsigned chip) {
    return n && n->initialized && chip<S5L8920_FMI_NO_CHIP && n->chip[chip].configured?
        &n->chip[chip]:NULL;
}
bool s5l8920_nand_init(s5l8920_nand_t *n) {
    if(!n)return false;
    memset(n,0,sizeof *n);n->initialized=true;return true;
}
static bool same(const s5l8920_nand_config_t *a,const s5l8920_nand_config_t *b) {
    return a->pages==b->pages && a->page_count==b->page_count &&
        a->row_count==b->row_count && a->row_bytes==b->row_bytes &&
        !memcmp(a->id,b->id,sizeof a->id) && a->busy_status==b->busy_status &&
        a->ready_status==b->ready_status && a->cache==b->cache && a->cache_capacity==b->cache_capacity;
}
static bool cache_disjoint(const s5l8920_nand_config_t *cache,const s5l8920_nand_config_t *image) {
    if(overlap(cache->cache,cache->row_bytes,image->pages,image->page_count*sizeof *image->pages))return false;
    for(size_t j=0;j<image->page_count;j++)
        if(overlap(cache->cache,cache->row_bytes,image->pages[j].bytes,image->row_bytes))return false;
    return true;
}
bool s5l8920_nand_configure(s5l8920_nand_t *n,unsigned chip,const s5l8920_nand_config_t *c) {
    if(!n || !n->initialized || chip>=S5L8920_FMI_NO_CHIP || !c)return false;
    if(n->chip[chip].configured)return same(&n->chip[chip].config,c);
    if(!c->row_count || c->row_count>0x1000000u || !c->row_bytes ||
       !c->page_count || c->page_count>c->row_count || c->page_count>SIZE_MAX/sizeof *c->pages ||
       !span(c->pages,c->page_count*sizeof *c->pages) || c->cache_capacity<c->row_bytes ||
       !span(c->cache,c->row_bytes) || (c->busy_status&0x40u) || !(c->ready_status&0x40u) ||
       overlap(c->cache,c->row_bytes,n,sizeof *n) ||
       overlap(c->pages,c->page_count*sizeof *c->pages,n,sizeof *n))return false;
    for(size_t j=0;j<c->page_count;j++) {
        const s5l8920_nand_page_t *p=&c->pages[j];
        if(p->row>=c->row_count || (j && p->row<=c->pages[j-1u].row) ||
           !span(p->bytes,c->row_bytes) || overlap(p->bytes,c->row_bytes,n,sizeof *n))return false;
    }
    if(!cache_disjoint(c,c))return false;
    for(unsigned j=0;j<S5L8920_FMI_NO_CHIP;j++)if(n->chip[j].configured) {
        const s5l8920_nand_config_t *old=&n->chip[j].config;
        if(overlap(c->cache,c->row_bytes,old->cache,old->row_bytes) ||
           !cache_disjoint(c,old) || !cache_disjoint(old,c))return false;
    }
    n->chip[chip].config=*c;n->chip[chip].configured=true;return true;
}
static bool command(void *owner,unsigned chip,uint8_t cmd,uint8_t address,uint8_t id[8]) {
    s5l8920_nand_chip_t *c=selected(owner,chip);
    if(!c)return false;
    if(cmd==0xffu) {
        if(c->pending==RESET)return false;
        c->source=NULL;c->copied=c->cursor=0;c->cache_valid=false;
        c->status_mode=false;c->reset_seen=true;c->pending=RESET;return true;
    }
    if(cmd!=0x90u || address || !id || !c->reset_seen || c->pending)return false;
    memcpy(id,c->config.id,8);c->source=NULL;c->cache_valid=false;
    c->copied=c->cursor=0;c->status_mode=false;return true;
}
static bool read_page(void *owner,unsigned chip,uint32_t row) {
    s5l8920_nand_chip_t *c=selected(owner,chip);
    if(!c || !c->reset_seen || c->pending || row>=c->config.row_count)return false;
    size_t lo=0,hi=c->config.page_count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2u;
        if(c->config.pages[mid].row<row)lo=mid+1u;else hi=mid;
    }
    if(lo==c->config.page_count || c->config.pages[lo].row!=row)return false;
    c->source=c->config.pages[lo].bytes;c->copied=c->cursor=0;
    c->cache_valid=false;c->status_mode=false;c->pending=READ;return true;
}
bool s5l8920_nand_service(s5l8920_nand_t *n,unsigned chip) {
    s5l8920_nand_chip_t *c=selected(n,chip);
    if(!c || !c->pending)return false;
    if(c->pending==RESET) {c->pending=IDLE;return true;}
    uint32_t remaining=c->config.row_bytes-c->copied,count=remaining<16u?remaining:16u;
    memcpy(c->config.cache+c->copied,c->source+c->copied,count);c->copied+=count;
    if(c->copied==c->config.row_bytes) {c->pending=IDLE;c->cache_valid=true;}
    return true;
}
static bool status_command(void *owner,unsigned chip,uint8_t cmd) {
    s5l8920_nand_chip_t *c=selected(owner,chip);
    if(!c || !c->reset_seen)return false;
    if(cmd==0x70u) {c->status_mode=true;return true;}
    if(cmd!=0u || c->pending || !c->cache_valid)return false;
    c->status_mode=false;return true;
}
static bool status_sample(void *owner,unsigned chip,uint8_t *status) {
    s5l8920_nand_chip_t *c=selected(owner,chip);
    if(!c || !c->reset_seen || !c->status_mode || !status)return false;
    *status=c->pending?c->config.busy_status:c->config.ready_status;return true;
}
s5l8920_fmi_data_result_t s5l8920_nand_receive(void *owner,unsigned chip,uint8_t *bytes,unsigned count) {
    s5l8920_nand_chip_t *c=selected(owner,chip);
    if(!c || !bytes || !count || count>16u || !c->reset_seen || c->status_mode)
        return S5L8920_FMI_DATA_UNAVAILABLE;
    if(c->pending==READ)return S5L8920_FMI_DATA_WAIT;
    if(!c->cache_valid || count>c->config.row_bytes-c->cursor)return S5L8920_FMI_DATA_UNAVAILABLE;
    memcpy(bytes,c->config.cache+c->cursor,count);c->cursor+=count;return S5L8920_FMI_DATA_READY;
}
const s5l8920_fmi_endpoint_t s5l8920_nand_endpoint={command,read_page};
const s5l8920_fmi_status_device_t s5l8920_nand_status={status_command,status_sample};
