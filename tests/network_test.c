#include "../src/control/network.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    struct dwa_net_bridge *b=calloc(1,sizeof(*b));struct dwa_net_frame f;
    struct dwa_net_request r;unsigned char e[1514]={0},mac[6]={2,1,2,3,4,5};unsigned int i;
    assert(b);memcpy(e+6,mac,6);e[0]=255;e[12]=8;
    assert(!dwa_net_push(b,0,e,60,0));dwa_net_open(b,mac,0xfffffff0U);
    assert(dwa_net_active(b,20));assert(dwa_net_push(b,0,e,60,20));
    assert(dwa_net_pop(b,0,&f,21)&&f.length==60&&!memcmp(f.data,e,60));
    assert(!dwa_net_push(b,0,e,13,21));assert(!dwa_net_push(b,0,e,1515,21));
    e[6]^=1;assert(!dwa_net_push(b,0,e,60,21));e[6]^=1;
    e[12]=0x88;e[13]=0x8e;assert(!dwa_net_push(b,0,e,60,21));e[12]=0x80;e[13]=0xf3;assert(!dwa_net_push(b,0,e,60,21));
    e[12]=0x81;e[13]=0x37;assert(!dwa_net_push(b,0,e,60,21));e[12]=8;e[13]=0;
    e[0]=2;assert(!dwa_net_push(b,1,e,60,21));memcpy(e,mac,6);
    assert(dwa_net_push(b,1,e,60,21));assert(dwa_net_pop(b,1,&f,22));
    for(i=0;i<DWA_NET_SLOTS;++i)assert(dwa_net_push(b,0,e,60,21));
    assert(!dwa_net_push(b,0,e,60,21)&&b->tx.dropped==1);
    assert(!dwa_net_pop(b,0,&f,2100)&&b->tx.dropped==33);
    assert(!dwa_net_active(b,5000));assert(!dwa_net_push(b,0,e,60,5000));
    dwa_net_reset(b);assert(!dwa_net_active(b,0)&&!b->tx.count&&!b->rx.count);
    memset(&r,0,sizeof(r));r.version=DWA_NET_VERSION;r.operation=DWA_NET_OPEN;assert(dwa_net_validate(&r));
    r.data[1515]=1;assert(!dwa_net_validate(&r));r.data[1515]=0;r.operation=6;assert(!dwa_net_validate(&r));
    r.operation=DWA_NET_RX;r.length=1515;assert(!dwa_net_validate(&r));r.length=1514;assert(dwa_net_validate(&r));
    free(b);puts("PASS: network bridge ownership boundary, bounds, queue pressure, expiry, clock wrap and wipe");return 0;
}
