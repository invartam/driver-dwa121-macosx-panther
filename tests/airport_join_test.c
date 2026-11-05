#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/control/airport_join.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    unsigned char raw[58];struct dwa_join_selection s;
    static struct rtl_bss_table t;struct rtl_bss b;unsigned int i;
    memset(raw,0,sizeof(raw));raw[0]=raw[57]=0xa5;
    raw[22]=1;memcpy(raw+23,"NAEBA",5);
    for(i=0;i<56;++i)CHECK(!dwa_join_parse(raw+1,i,&s));
    CHECK(!dwa_join_parse(raw+1,57,&s));
    CHECK(dwa_join_parse(raw+1,56,&s)&&s.ssid_length==5&&!s.legacy_prompt);
    CHECK(raw[0]==0xa5&&raw[57]==0xa5);
    raw[2]=1;raw[4]=13;raw[22]=2;memset(raw+5,0x5a,13);
    CHECK(dwa_join_parse(raw+1,56,&s)&&s.legacy_prompt);
    CHECK(!memcmp(s.ssid,"NAEBA",5)&&s.ssid[5]==0);
    raw[4]=5;CHECK(!dwa_join_parse(raw+1,56,&s));raw[4]=13;
    raw[4]=5;raw[22]=3;memset(raw+5,0,16);
    CHECK(dwa_join_parse(raw+1,56,&s)&&s.legacy_prompt==2);
    raw[5]=1;CHECK(!dwa_join_parse(raw+1,56,&s));raw[5]=0;
    raw[4]=13;raw[22]=2;
    raw[30]=1;CHECK(!dwa_join_parse(raw+1,56,&s));raw[30]=0;
    memset(raw+23,'x',32);CHECK(!dwa_join_parse(raw+1,56,&s));
    memset(raw+1,0,56);raw[22]=1;memset(raw+23,255,4);
    CHECK(dwa_join_parse(raw+1,56,&s)&&s.operation==DWA_JOIN_DISCONNECT);
    raw[27]=1;CHECK(!dwa_join_parse(raw+1,56,&s));
    t.count=2;b=t.entries[0];b.ssid_length=5;memcpy(b.ssid,"NAEBA",5);
    b.bssid[0]=2;b.bssid[5]=9;b.channel=11;b.capabilities=0x11;
    b.last_seen_ms=100;b.rsn.status=RTL_RSN_VALID;
    b.rsn.group_ccmp=b.rsn.pairwise_ccmp=b.rsn.psk=1;
    t.entries[0]=b;b.bssid[5]=1;b.channel=1;t.entries[1]=b;
    CHECK(dwa_join_choose(&t,(const rtl_u8 *)"NAEBA",5,11,101,&b)&&b.channel==11);
    CHECK(dwa_join_choose(&t,(const rtl_u8 *)"NAEBA",5,6,101,&b)&&b.bssid[5]==1);
    CHECK(!dwa_join_choose(&t,(const rtl_u8 *)"OTHER",5,11,101,&b));
    CHECK(!dwa_join_choose(&t,(const rtl_u8 *)"NAEBA",5,11,15100,&b));
    t.entries[0].rsn.capabilities=0x40;t.entries[1].rsn.psk=0;
    CHECK(!dwa_join_choose(&t,(const rtl_u8 *)"NAEBA",5,11,101,&b));
    t.entries[0].rsn.capabilities=0;t.entries[0].last_seen_ms=0xfffffff0U;
    CHECK(dwa_join_choose(&t,(const rtl_u8 *)"NAEBA",5,11,20,&b));
    t.count=RTL_BSS_MAX+1;CHECK(!dwa_join_choose(&t,(const rtl_u8 *)"NAEBA",5,11,20,&b));
    puts("PASS: native join bounds/BE/padding, secret exclusion, disconnect, SSID identity, BSSID policy, expiry/wrap, security rejection");return 0;
}
