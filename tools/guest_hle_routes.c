/* Copyright (c) 2026 j0shua-SYSON. MIT licensed. */
#include "guest_hle_routes.h"
#include <string.h>
#include <errno.h>
static uint64_t be64(const unsigned char *p) {
    uint64_t n=0; for (unsigned i=0;i<8;i++) n=(n<<8)|p[i]; return n;
}
static void expire(s5lbox_hle_routes *s,double now) {
    if (now>=s->lease_until || now>=s->ticket_until) { s->ticket=0; s->pending[0]=0; }
}
static int selected(s5lbox_hle_routes *s,const char *identifier) {
    for (unsigned i=0;i<s->count;i++) if (!strcmp(identifier,s->selected[i])) return 1;
    return 0;
}
int s5lbox_hle_route_icon(s5lbox_hle_routes *s,const char *identifier,double now) {
    expire(s,now);
    if (!identifier || now>=s->lease_until || !selected(s,identifier)) return 0;
    if (!s->ticket) {
        if (!++s->serial) ++s->serial;
        s->ticket=s->serial; strcpy(s->pending,identifier); s->ticket_until=now+30;
    }
    return 1;
}
size_t s5lbox_hle_route_request(s5lbox_hle_routes *s,const unsigned char *p,size_t n,
        double now,unsigned char out[HLE_ROUTE_MAX_REPLY],
        int (*native)(void *,const char *),void *context) {
    int error=EINVAL; unsigned count=0;
    /* Validate everything before changing any existing selection or ticket. */
    const unsigned char *names[HLE_ROUTE_MAX_APPS]; unsigned lengths[HLE_ROUTE_MAX_APPS];
    if (n<27 || n>HLE_ROUTE_MAX_REQUEST || p[0]<1 || p[0]>4) goto done;
    count=((unsigned)p[25]<<8)|p[26];
    if (count>HLE_ROUTE_MAX_APPS || (p[0]==3?count!=1:p[0]!=1&&count!=0)) goto done;
    size_t offset=27;
    for (unsigned i=0;i<count;i++) {
        if (n-offset<2) goto done;
        unsigned length=((unsigned)p[offset]<<8)|p[offset+1]; offset+=2;
        if (!length || length>255 || n-offset<length) goto done;
        for (unsigned j=0;j<length;j++) {
            unsigned c=p[offset+j];
            if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) goto done;
        }
        for (unsigned j=0;j<i;j++)
            if (lengths[j]==length && !memcmp(names[j],p+offset,length)) goto done;
        names[i]=p+offset; lengths[i]=length; offset+=length;
    }
    if (offset!=n) goto done;
    expire(s,now);
    if (p[0]!=1 && memcmp(s->session,p+1,16)) { error=EAGAIN; goto done; }
    if (p[0]==1) {
        if (memcmp(s->session,p+1,16)) { s->ticket=0; s->pending[0]=0; }
        memcpy(s->session,p+1,16); s->count=count;
        for (unsigned i=0;i<count;i++) {
            memcpy(s->selected[i],names[i],lengths[i]); s->selected[i][lengths[i]]=0;
        }
        if (s->ticket && !selected(s,s->pending)) { s->ticket=0; s->pending[0]=0; }
        s->lease_until=now+10;
    } else if (p[0]==2) {
        if (s->ticket && s->ticket!=be64(p+17)) { error=EAGAIN; goto done; }
        s->ticket=0; s->pending[0]=0;
    } else if (p[0]==3) {
        char identifier[256]; memcpy(identifier,names[0],lengths[0]); identifier[lengths[0]]=0;
        error=native?native(context,identifier):ENOTSUP; goto done;
    } else { s->count=0; s->lease_until=0; s->ticket=0; s->pending[0]=0; }
    error=0;
done:
    memcpy(out,"HLR1",4);
    for (unsigned i=0;i<4;i++) out[4+i]=(unsigned char)((unsigned)error>>(24-i*8));
    uint64_t ticket=error?0:s->ticket;
    for (unsigned i=0;i<8;i++) out[8+i]=(unsigned char)(ticket>>(56-i*8));
    unsigned length=ticket?(unsigned)strlen(s->pending):0;
    out[16]=(unsigned char)(length>>8); out[17]=(unsigned char)length;
    memcpy(out+18,s->pending,length); return 18+length;
}
