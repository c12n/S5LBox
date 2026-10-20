/* Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "../../../tools/guest_hle_routes.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
static unsigned failures,launches;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); failures++; } } while (0)
static int native(void *context,const char *app) {
    CHECK(context==&launches); CHECK(!strcmp(app,"example.app")); launches++; return 0;
}
static size_t request(unsigned char *p,unsigned op,unsigned session,uint64_t ticket,const char *name) {
    memset(p,0,HLE_ROUTE_MAX_REQUEST); p[0]=(unsigned char)op; p[1]=(unsigned char)session;
    for (unsigned i=0;i<8;i++) p[17+i]=(unsigned char)(ticket>>(56-8*i));
    if (!name) return 27;
    size_t n=strlen(name); p[26]=1; p[27]=(unsigned char)(n>>8); p[28]=(unsigned char)n;
    memcpy(p+29,name,n); return 29+n;
}
int main(void) {
    s5lbox_hle_routes s={0}; unsigned char p[HLE_ROUTE_MAX_REQUEST],out[HLE_ROUTE_MAX_REPLY];
    size_t n=request(p,1,1,0,"example.app");
    CHECK(!s5lbox_hle_route_icon(&s,"example.app",1));
    CHECK(s5lbox_hle_route_request(&s,p,n,1,out,native,&launches)==18 && !out[7]);
    CHECK(!s5lbox_hle_route_icon(&s,"native.app",2));
    CHECK(s5lbox_hle_route_icon(&s,"example.app",2)); uint64_t ticket=s.ticket;
    CHECK(s5lbox_hle_route_icon(&s,"example.app",2) && s.ticket==ticket);
    CHECK(s5lbox_hle_route_request(&s,p,n,3,out,native,&launches)>18 && s.ticket==ticket);
    /* Polls do not consume; a lost response cannot lose the launch. */
    CHECK(s5lbox_hle_route_request(&s,p,n,4,out,native,&launches)>18 && s.ticket==ticket);
    n=request(p,2,2,ticket,NULL); s5lbox_hle_route_request(&s,p,n,4,out,native,&launches);
    CHECK(out[7] && s.ticket==ticket);
    n=request(p,2,1,ticket+1,NULL); s5lbox_hle_route_request(&s,p,n,4,out,native,&launches);
    CHECK(out[7] && s.ticket==ticket);
    n=request(p,2,1,ticket,NULL); s5lbox_hle_route_request(&s,p,n,4,out,native,&launches);
    CHECK(!out[7] && !s.ticket);
    n=request(p,3,1,0,"example.app"); s5lbox_hle_route_request(&s,p,n,5,out,native,&launches);
    CHECK(!out[7] && launches==1);
    n=request(p,1,1,0,"bad/path"); s5lbox_hle_route_request(&s,p,n,5,out,native,&launches);
    CHECK(out[7] && !strcmp(s.selected[0],"example.app"));
    CHECK(s5lbox_hle_route_icon(&s,"example.app",6));
    CHECK(!s5lbox_hle_route_icon(&s,"example.app",15) && !s.ticket);
    n=request(p,1,2,0,"example.app"); s5lbox_hle_route_request(&s,p,n,16,out,native,&launches);
    CHECK(s5lbox_hle_route_icon(&s,"example.app",17));
    n=request(p,1,3,0,"example.app"); s5lbox_hle_route_request(&s,p,n,18,out,native,&launches);
    CHECK(!s.ticket); /* A resumed/new host cannot execute an old ticket. */
    CHECK(s5lbox_hle_route_icon(&s,"example.app",19));
    n=request(p,1,3,0,NULL); s5lbox_hle_route_request(&s,p,n,20,out,native,&launches);
    CHECK(!s.ticket && !s5lbox_hle_route_icon(&s,"example.app",21));
    n=request(p,1,3,0,"example.app");
    for (size_t i=0;i<n;i++) { s5lbox_hle_route_request(&s,p,i,22,out,native,&launches); CHECK(out[7]); }
    s5lbox_hle_route_request(&s,p,n,22,out,native,&launches);
    n=request(p,4,3,0,NULL); s5lbox_hle_route_request(&s,p,n,23,out,native,&launches);
    CHECK(!s5lbox_hle_route_icon(&s,"example.app",23));
    printf("S5LBOX-HLE-ROUTES %s failures=%u\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
