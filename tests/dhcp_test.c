#include "../src/net80211/dhcp_probe.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
static const rtl_u8 mac[6]={2,1,2,3,4,5},xid[4]={0x12,0x34,0xab,0xcd};
static unsigned int checksum(const rtl_u8 *p,unsigned int n,unsigned int sum)
{
    unsigned int i;for(i=0;i<n;++i)sum+=(unsigned int)p[i]<<(i%2?0:8);
    while(sum>65535)sum=(sum&65535)+(sum>>16);return (~sum)&65535;
}
static void put16(rtl_u8 *p,unsigned int v){p[0]=(rtl_u8)(v>>8);p[1]=(rtl_u8)v;}
static void checksums(rtl_u8 *e)
{
    unsigned int s;rtl_u8 pseudo[12];
    e[24]=e[25]=0;put16(e+24,checksum(e+14,20,0));
    memcpy(pseudo,e+26,8);pseudo[8]=0;pseudo[9]=17;pseudo[10]=1;pseudo[11]=52;
    e[40]=e[41]=0;s=checksum(pseudo,12,0)^65535;s=checksum(e+34,308,s);put16(e+40,s?s:65535);
}
static void offer(rtl_u8 *e)
{
    rtl_u8 *b=e+42;memset(e,0,342);memset(e,255,6);memcpy(e+6,"\x02\x06\x07\x08\x09\x0a",6);
    e[12]=8;e[14]=0x45;e[16]=1;e[17]=72;e[22]=64;e[23]=17;
    memcpy(e+26,"\xc0\xa8\x01\x01",4);memset(e+30,255,4);
    e[35]=67;e[37]=68;e[38]=1;e[39]=52;
    b[0]=2;b[1]=1;b[2]=6;memcpy(b+4,xid,4);memcpy(b+16,"\xc0\xa8\x01\x64",4);memcpy(b+28,mac,6);
    memcpy(b+236,"\x63\x82\x53\x63\x35\x01\x02\x36\x04\xc0\xa8\x01\x01\xff",14);
    checksums(e);
}
static void rejects(const rtl_u8 *e,unsigned int n)
{
    struct dwa_dhcp_offer o;unsigned int i;memset(&o,0xa5,sizeof(o));
    assert(dwa_dhcp_parse_offer(e,n,mac,xid,&o)!=0);
    for(i=0;i<sizeof(o);++i)assert(!((const rtl_u8 *)&o)[i]);
}
int main(void)
{
    rtl_u8 buf[344],e[342],bad[342];struct dwa_dhcp_offer o;
    unsigned int i,j,n,seed=77;
    static const unsigned int fields[]={12,13,14,20,21,23,30,35,37,38,39,42,43,44,46,70,278,280,283,284,286};
    memset(buf,0x55,sizeof(buf));assert(!dwa_dhcp_discover(buf+1,342,mac,xid,7));
    assert(buf[0]==0x55&&buf[343]==0x55);memcpy(e,buf+1,342);
    assert(!checksum(e+14,20,0)&&e[35]==68&&e[37]==67&&e[42]==1&&e[52]==0x80&&e[51]==7);
    assert(!memcmp(e+46,xid,4)&&!memcmp(e+70,mac,6));
    memcpy(bad,e,342);checksums(bad);assert(!memcmp(e,bad,342));rejects(e,342);
    assert(dwa_dhcp_discover(e,341,mac,xid,0)!=0&&dwa_dhcp_discover(e,342,mac,xid,65536)!=0);
    offer(e);assert(!dwa_dhcp_parse_offer(e,342,mac,xid,&o));
    assert(o.address[3]==100&&o.server[3]==1);
    for(i=0;i<342;++i)rejects(e,i);
    for(i=0;i<sizeof(fields)/sizeof(fields[0]);++i){memcpy(bad,e,342);bad[fields[i]]^=0x80;checksums(bad);rejects(bad,342);}
    memcpy(bad,e,342);bad[41]^=1;rejects(bad,342);
    memcpy(bad,e,342);bad[25]^=1;rejects(bad,342);
    memcpy(bad,e,342);bad[40]=bad[41]=0;assert(!dwa_dhcp_parse_offer(bad,342,mac,xid,&o));
    memcpy(bad,e,342);memcpy(bad,mac,6);memcpy(bad+30,bad+58,4);checksums(bad);
    assert(!dwa_dhcp_parse_offer(bad,342,mac,xid,&o));
    memcpy(bad,e,342);bad[283]=255;checksums(bad);rejects(bad,342); /* length overrun */
    memcpy(bad,e,342);memset(bad+291,0,51);checksums(bad);rejects(bad,342); /* no END */
    memcpy(bad,e,342);memcpy(bad+291,"\x35\x01\x02\xff",4);checksums(bad);rejects(bad,342);
    memcpy(bad,e,342);memcpy(bad+291,"\x34\x01\x01\xff",4);checksums(bad);rejects(bad,342);
    for(i=0;i<10000;++i){
        seed=seed*1664525U+1013904223U;n=seed%342;
        for(j=0;j<n;++j){seed=seed*1664525U+1013904223U;bad[j]=(rtl_u8)(seed>>24);}
        rejects(bad,n);
    }
    puts("PASS: DHCP discovery/OFFER IP+UDP checksums, xid/chaddr, destination/ports/options, no lease request, truncations, malformed options, 10000 bounded inputs");
    return 0;
}
