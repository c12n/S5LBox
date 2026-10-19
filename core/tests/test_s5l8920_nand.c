/* Real cache work drives NAND status and the existing FMI byte/IRQ path.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "s5l8920_nand.h"
#include <stdio.h>
#include <string.h>

static unsigned passed,failed;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %s:%d %s\n",__func__,__LINE__,#x);}} while(0)
static s5l8920_nand_t nand;
static s5l8920_fmi_t fmi;
static uint8_t raw[2][4314],cache[8][4314];
static s5l8920_nand_page_t pages[2];
static s5l8920_nand_config_t config;
static void setup(unsigned length) {
    CHECK(s5l8920_nand_init(&nand));
    memset(cache,0xa5,sizeof cache);
    for(unsigned j=0;j<2;j++)for(unsigned k=0;k<sizeof raw[j];k++)raw[j][k]=(uint8_t)(j*71u+k*11u+(k>>8));
    pages[0]=(s5l8920_nand_page_t){0,raw[0]};pages[1]=(s5l8920_nand_page_t){0xffffffu,raw[1]};
    config=(s5l8920_nand_config_t){.pages=pages,.page_count=2,.row_count=0x1000000u,
        .row_bytes=length,.id={0xa1,0xb2,0xc3,0xd4,5,6,7,8},.busy_status=0x80,.ready_status=0xe0,
        .cache=cache[0],.cache_capacity=sizeof cache[0]};
}
static void reset(unsigned chip) {
    CHECK(s5l8920_nand_endpoint.command(&nand,chip,0xff,0,NULL));
    CHECK(s5l8920_nand_service(&nand,chip));
}
static void invalid_config(unsigned chip,s5l8920_nand_config_t *c) {
    s5l8920_nand_t before=nand;
    CHECK(!s5l8920_nand_configure(&nand,chip,c));CHECK(!memcmp(&before,&nand,sizeof nand));
}
static void test_configuration(void) {
    setup(544);CHECK(!s5l8920_nand_init(NULL));
    CHECK(!s5l8920_nand_configure(NULL,0,&config));invalid_config(8,&config);invalid_config(0,NULL);
    s5l8920_nand_t cold={0};CHECK(!s5l8920_nand_configure(&cold,0,&config));
    for(unsigned kind=0;kind<16;kind++) {
        s5l8920_nand_config_t c=config;
        switch(kind) {
        case 0:c.row_count=0;break;
        case 1:c.row_count=0x1000001u;break;
        case 2:c.row_count=0xffffffu;break;
        case 3:c.row_bytes=0;break;
        case 4:c.cache_capacity=543;break;
        case 5:c.cache=NULL;break;
        case 6:c.pages=NULL;break;
        case 7:c.page_count=0;break;
        case 8:c.page_count=SIZE_MAX;break;
        case 9:c.busy_status|=0x40;break;
        case 10:c.ready_status&=(uint8_t)~0x40u;break;
        case 11:c.cache=(uint8_t*)&nand;break;
        case 12:c.cache=(uint8_t*)pages;break;
        case 13:c.cache=raw[0]+1;break;
        case 14:c.cache=(uint8_t*)(uintptr_t)(UINTPTR_MAX-8u);break;
        default:c.pages=(const s5l8920_nand_page_t*)&nand;break;
        }
        invalid_config(0,&c);
    }
    pages[1].row=0;invalid_config(0,&config);pages[1].row=0xffffffu;
    pages[0].row=0xffffffu;pages[1].row=0;invalid_config(0,&config);
    pages[0].row=0;pages[1].row=0xffffffu;
    pages[1].bytes=NULL;invalid_config(0,&config);pages[1].bytes=(const uint8_t*)&nand;
    invalid_config(0,&config);pages[1].bytes=raw[1];
    CHECK(s5l8920_nand_configure(&nand,0,&config));
    reset(0);CHECK(s5l8920_nand_endpoint.read_page(&nand,0,0));CHECK(s5l8920_nand_service(&nand,0));
    s5l8920_nand_t before=nand;
    CHECK(s5l8920_nand_configure(&nand,0,&config));CHECK(!memcmp(&before,&nand,sizeof nand));
    s5l8920_nand_config_t changed=config;changed.ready_status^=1;invalid_config(0,&changed);
    invalid_config(1,&config); /* Shared mutable cache refuses across chips. */
    for(unsigned chip=1;chip<8;chip++) {
        changed=config;changed.cache=cache[chip];changed.id[7]=(uint8_t)chip;
        CHECK(s5l8920_nand_configure(&nand,chip,&changed));reset(chip);
        uint8_t id[8];CHECK(s5l8920_nand_endpoint.command(&nand,chip,0x90,0,id));CHECK(id[7]==chip);
    }
    CHECK(nand.chip[0].copied==16 && nand.chip[0].pending && !nand.chip[0].cache_valid);
    uint8_t saved[sizeof cache];memcpy(saved,cache,sizeof cache);
    CHECK(s5l8920_nand_init(&nand));CHECK(!memcmp(saved,cache,sizeof cache));
    CHECK(!s5l8920_nand_service(&nand,0));
    /* A new cache must not overwrite another chip's immutable source, and
     * a new source/descriptor must not sit inside an existing mutable cache. */
    setup(16);CHECK(s5l8920_nand_configure(&nand,0,&config));
    s5l8920_nand_page_t other={3,raw[0]+32};
    changed=config;changed.pages=&other;changed.page_count=1;changed.cache=raw[1];invalid_config(1,&changed);
    changed.cache=cache[1];other.bytes=cache[0];invalid_config(1,&changed);
    /* A properly aligned, typed descriptor is also writable byte storage. */
    s5l8920_nand_page_t descriptors[2]={{3,raw[0]+32},{4,raw[1]+32}};
    setup(16);config.cache=(uint8_t*)descriptors;config.cache_capacity=sizeof descriptors;
    CHECK(s5l8920_nand_configure(&nand,0,&config));
    changed=config;changed.cache=cache[1];changed.cache_capacity=sizeof cache[1];
    changed.pages=descriptors;invalid_config(1,&changed);
}
static void test_streams(void) {
    const unsigned lengths[]={1,15,16,17,544,545,4314};
    for(unsigned size=0;size<sizeof lengths/sizeof lengths[0];size++)for(unsigned chip=0;chip<8;chip++) {
        unsigned length=lengths[size];setup(length);config.cache=cache[chip];
        CHECK(s5l8920_nand_configure(&nand,chip,&config));
        uint8_t value=0x96,id[8]={0},bytes[16];memset(bytes,0x96,sizeof bytes);
        s5l8920_nand_t before=nand;
        CHECK(!s5l8920_nand_status.sample(&nand,chip,&value) && value==0x96);
        CHECK(!s5l8920_nand_status.command(&nand,chip,0x70));
        CHECK(!s5l8920_nand_endpoint.command(&nand,chip,0x90,0,id));
        CHECK(!s5l8920_nand_endpoint.read_page(&nand,chip,0));
        CHECK(s5l8920_nand_receive(&nand,chip,bytes,1)==S5L8920_FMI_DATA_UNAVAILABLE);
        CHECK(!memcmp(&before,&nand,sizeof nand));
        CHECK(s5l8920_nand_endpoint.command(&nand,chip,0xff,0,NULL));
        CHECK(s5l8920_nand_status.command(&nand,chip,0x70));
        CHECK(s5l8920_nand_status.sample(&nand,chip,&value) && value==0x80);
        before=nand;CHECK(!s5l8920_nand_endpoint.command(&nand,chip,0xff,0,NULL));
        CHECK(!s5l8920_nand_endpoint.read_page(&nand,chip,0));CHECK(!memcmp(&before,&nand,sizeof nand));
        CHECK(s5l8920_nand_service(&nand,chip));
        CHECK(s5l8920_nand_status.sample(&nand,chip,&value) && value==0xe0);
        CHECK(!s5l8920_nand_endpoint.command(&nand,chip,0x90,1,id));
        CHECK(!s5l8920_nand_endpoint.command(&nand,chip,0x90,0,NULL));
        CHECK(!s5l8920_nand_endpoint.command(&nand,chip,0x80,0,id));
        CHECK(s5l8920_nand_endpoint.command(&nand,chip,0x90,0,id));CHECK(!memcmp(id,config.id,8));
        for(unsigned p=0;p<2;p++) {
            before=nand;
            CHECK(!s5l8920_nand_endpoint.read_page(&nand,chip,1));
            CHECK(!s5l8920_nand_endpoint.read_page(&nand,chip,0x1000000u));
            CHECK(!memcmp(&before,&nand,sizeof nand));
            CHECK(s5l8920_nand_endpoint.read_page(&nand,chip,pages[p].row));
            before=nand;
            CHECK(s5l8920_nand_receive(&nand,chip,bytes,1)==S5L8920_FMI_DATA_WAIT);
            CHECK(bytes[0]==0x96);
            CHECK(!s5l8920_nand_endpoint.command(&nand,chip,0x90,0,id));
            CHECK(!s5l8920_nand_endpoint.read_page(&nand,chip,pages[p].row));
            CHECK(!memcmp(&before,&nand,sizeof nand));
            CHECK(s5l8920_nand_status.command(&nand,chip,0x70));
            for(unsigned copied=0;copied<length;) {
                before=nand;
                for(unsigned poll=0;poll<3;poll++)CHECK(s5l8920_nand_status.sample(&nand,chip,&value) && value==0x80);
                CHECK(!s5l8920_nand_status.command(&nand,chip,0));
                CHECK(!memcmp(&before,&nand,sizeof nand));
                CHECK(s5l8920_nand_service(&nand,chip));copied+=length-copied<16?length-copied:16;
                CHECK(nand.chip[chip].copied==copied && !memcmp(cache[chip],raw[p],copied));
                CHECK(nand.chip[chip].cache_valid==(copied==length));
            }
            CHECK(s5l8920_nand_status.sample(&nand,chip,&value) && value==0xe0);
            CHECK(s5l8920_nand_receive(&nand,chip,bytes,1)==S5L8920_FMI_DATA_UNAVAILABLE);
            CHECK(s5l8920_nand_status.command(&nand,chip,0));
            for(unsigned cursor=0;cursor<length;) {
                unsigned count=length-cursor<13?length-cursor:13;
                CHECK(s5l8920_nand_receive(&nand,chip,bytes,count)==S5L8920_FMI_DATA_READY);
                CHECK(!memcmp(bytes,raw[p]+cursor,count));cursor+=count;
                CHECK(s5l8920_nand_status.command(&nand,chip,0x70));
                CHECK(s5l8920_nand_status.sample(&nand,chip,&value) && value==0xe0);
                CHECK(s5l8920_nand_status.command(&nand,chip,0));CHECK(nand.chip[chip].cursor==cursor);
            }
            before=nand;memset(bytes,0x96,sizeof bytes);
            CHECK(s5l8920_nand_receive(&nand,chip,bytes,1)==S5L8920_FMI_DATA_UNAVAILABLE);
            CHECK(s5l8920_nand_receive(&nand,chip,bytes,0)==S5L8920_FMI_DATA_UNAVAILABLE);
            CHECK(s5l8920_nand_receive(&nand,chip,bytes,17)==S5L8920_FMI_DATA_UNAVAILABLE);
            CHECK(s5l8920_nand_receive(&nand,chip,NULL,1)==S5L8920_FMI_DATA_UNAVAILABLE);
            CHECK(!s5l8920_nand_service(&nand,chip));
            CHECK(!memcmp(&before,&nand,sizeof nand) && bytes[0]==0x96);
        }
        for(unsigned k=length;k<sizeof cache[chip];k++)CHECK(cache[chip][k]==0xa5);
    }
    CHECK(!s5l8920_nand_service(NULL,0));CHECK(!s5l8920_nand_service(&nand,8));
    uint8_t value=0x96;
    CHECK(!s5l8920_nand_status.sample(&nand,8,&value) && value==0x96);
    CHECK(!s5l8920_nand_status.sample(&nand,7,NULL));CHECK(!s5l8920_nand_status.command(&nand,7,1));
    CHECK(!s5l8920_nand_endpoint.command(&nand,8,0xff,0,NULL));
    CHECK(!s5l8920_nand_endpoint.read_page(&nand,8,0));
    CHECK(!s5l8920_nand_endpoint.command(&nand,0,0xff,0,NULL));
    CHECK(!s5l8920_nand_endpoint.read_page(&nand,0,0));
    for(unsigned j=0;j<2;j++)for(unsigned k=0;k<sizeof raw[j];k++)
        CHECK(raw[j][k]==(uint8_t)(j*71u+k*11u+(k>>8)));
}
static void wr(unsigned off,uint32_t value) {
    bool okay=s5l8920_fmi_write(&fmi,off,value);
    if(!okay)printf("Refused FMI %03x=%08x\n",off,value);
    CHECK(okay);
}
static void service(void) {CHECK(s5l8920_fmi_service_io(&fmi,&s5l8920_nand_endpoint,&s5l8920_nand_status,&nand));}
static void test_controller(void) {
    setup(545);CHECK(s5l8920_nand_configure(&nand,2,&config));
    fmi=(s5l8920_fmi_t){0};CHECK(s5l8920_fmi_init(&fmi));wr(0x400,1);wr(0x408,0x11111);wr(0x40c,4);
    wr(0x414,0xff);wr(0x410,1);service();wr(0x444,1);
    CHECK(s5l8920_nand_service(&nand,2));
    wr(0x414,0x3000);wr(0x418,0);wr(0x41c,0);wr(0x420,4);wr(0x410,0xb);
    s5l8920_fmi_t before=fmi;
    CHECK(!s5l8920_fmi_service_endpoint(&fmi,NULL,NULL));CHECK(!memcmp(&before,&fmi,sizeof fmi));
    service();wr(0x444,0xb);CHECK(nand.chip[2].pending && !nand.chip[2].copied && !fmi.stream_bytes);
    wr(0x414,0x70);wr(0x410,1);service();wr(0x444,1);
    wr(0x44c,0x4040);wr(0x440,0x20);wr(0x10,0x100);wr(0x424,0);wr(0x410,0x50);
    service();CHECK(fmi.data==0x80 && !s5l8920_fmi_irq(&fmi));
    for(unsigned k=0;k<34;k++)CHECK(s5l8920_nand_service(&nand,2));
    CHECK(nand.chip[2].copied==544 && !nand.chip[2].cache_valid);
    service();CHECK(fmi.data==0x80 && !s5l8920_fmi_irq(&fmi));
    s5l8920_nand_t pending=nand;
    for(unsigned k=0;k<10;k++) {uint32_t v;CHECK(s5l8920_fmi_read(&fmi,0x448,&v) && v==0x80);}
    CHECK(!memcmp(&pending,&nand,sizeof nand));
    CHECK(s5l8920_nand_service(&nand,2));CHECK(!s5l8920_fmi_irq(&fmi));
    service();CHECK(fmi.data==0xe0 && s5l8920_fmi_irq(&fmi));
    wr(0xc,0xf);CHECK(s5l8920_fmi_irq(&fmi));wr(0x410,0);wr(0x444,0x20);CHECK(!s5l8920_fmi_irq(&fmi));
    wr(0x414,0);wr(0x410,1);service();wr(0x444,1);
    CHECK(s5l8920_fmi_data_configure(&fmi,16,16));wr(0x814,0);wr(0,0xaa083);wr(0x10,2);wr(4,3);
    uint8_t data[544];unsigned count[2]={0,0};
    for(unsigned k=0;k<34;k++) {
        CHECK(s5l8920_fmi_data_service(&fmi,s5l8920_nand_receive,&nand));
        before=fmi;pending=nand;
        if(k!=31 && k!=33) { /* This lane is full; another service cannot consume input. */
            CHECK(s5l8920_fmi_data_service(&fmi,s5l8920_nand_receive,&nand));
            CHECK(!memcmp(&before,&fmi,sizeof fmi) && !memcmp(&pending,&nand,sizeof nand));
        }
        unsigned lane=k<32?0:1,offset=lane?512:0;
        CHECK(s5l8920_fmi_data_receive(&fmi,lane,data+offset+count[lane],16)==S5L8920_FMI_DATA_READY);
        count[lane]+=16;CHECK(s5l8920_fmi_irq(&fmi)==(k==33));
    }
    CHECK(count[0]==512 && count[1]==32 && !memcmp(data,raw[0],544));
    CHECK(nand.chip[2].cursor==544 && fmi.stream_bytes==544 && fmi.sectors_completed==1);
    wr(0xc,2);CHECK(!s5l8920_fmi_irq(&fmi));
    uint8_t tail[2]={0x96,0x96};pending=nand;
    CHECK(s5l8920_nand_receive(&nand,2,tail,2)==S5L8920_FMI_DATA_UNAVAILABLE);
    CHECK(!memcmp(&pending,&nand,sizeof nand) && tail[0]==0x96);
    CHECK(s5l8920_nand_receive(&nand,2,tail,1)==S5L8920_FMI_DATA_READY && tail[0]==raw[0][544]);
    CHECK(s5l8920_nand_endpoint.read_page(&nand,2,0xffffffu));CHECK(s5l8920_nand_service(&nand,2));
    pending=nand;CHECK(s5l8920_fmi_reset_line(&fmi,true));CHECK(!memcmp(&pending,&nand,sizeof nand));
    uint8_t saved[545];memcpy(saved,cache[0],sizeof saved);
    reset(2);CHECK(!memcmp(saved,cache[0],sizeof saved) && !nand.chip[2].cache_valid && !nand.chip[2].copied);
    CHECK(s5l8920_nand_receive(&nand,2,tail,1)==S5L8920_FMI_DATA_UNAVAILABLE);
    CHECK(s5l8920_nand_endpoint.read_page(&nand,2,0));
    while(nand.chip[2].pending)CHECK(s5l8920_nand_service(&nand,2));
    CHECK(!memcmp(cache[0],raw[0],545));
}
int main(void) {
    test_configuration();test_streams();test_controller();
    printf("S5L8920 NAND: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
