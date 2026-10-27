#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/net80211/beacon.h"
#include "../src/rtl8188cu/rx.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const rtl_u8 rsn_psk[] = {
    48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0
};
static const rtl_u8 base[] = {
    0x80,0,0,0,255,255,255,255,255,255,2,1,2,3,4,5,
    2,1,2,3,4,5,0,0,0,0,0,0,0,0,0,0,100,0,0x11,0,
    0,5,'N','A','E','B','A',3,1,11
};
/* This separate fixed vector includes an externally generated zlib FCS. */
static const rtl_u8 verified[] = {
    0x80,0,0,0,255,255,255,255,255,255,2,0x11,0x22,0x33,0x44,0x55,
    2,0x11,0x22,0x33,0x44,0x55,0,0,0,0,0,0,0,0,0,0,100,0,0,0,
    0,2,'x','x',0xf2,0x88,0x51,0xef
};
static unsigned int fixture(rtl_u8 *f)
{memcpy(f,base,sizeof(base));memcpy(f+sizeof(base),rsn_psk,sizeof(rsn_psk));return sizeof(base)+sizeof(rsn_psk);}
static void fresh(struct rtl_bss_table *t){memset(t,0,sizeof(*t));}
static void sink(void *cookie,const rtl_u8 *f,unsigned int n)
{rtl_bss_observe((struct rtl_bss_table *)cookie,f,n,11);}
int main(void)
{
    static struct rtl_bss_table t;
    rtl_u8 f[4093],ie[258],usb[256];struct rtl_rsn r;struct rtl_rx_stats stats;
    unsigned int n,i,j,len,seed=0x19561234;
    CHECK(rtl_rsn_parse(rsn_psk,sizeof(rsn_psk),&r)==0);
    CHECK(r.status==RTL_RSN_VALID&&r.group_ccmp&&r.pairwise_ccmp&&r.psk&&!r.capabilities);
    /* Every truncation with stale IE length is rejected, without partial flags. */
    for(i=0;i<sizeof(rsn_psk);++i){
        CHECK(rtl_rsn_parse(rsn_psk,i,&r)!=0&&r.status==RTL_RSN_MALFORMED&&!r.psk);
    }
    for(i=4;i<=sizeof(rsn_psk);++i){
        memcpy(ie,rsn_psk,i);ie[1]=(rtl_u8)(i-2);
        n=rtl_rsn_parse(ie,i,&r)==0;
        CHECK(n==(i==4||i==8||i==14||i==20||i==22));
        if(i<20)CHECK(!r.psk);
    }
    memcpy(ie,rsn_psk,sizeof(rsn_psk));ie[2]=2;
    CHECK(rtl_rsn_parse(ie,22,&r)==RTL_UNSUPPORTED&&r.status==RTL_RSN_UNSUPPORTED&&!r.psk);
    for(i=8;i<=14;i+=6){
        memcpy(ie,rsn_psk,22);ie[i]=ie[i+1]=255;
        CHECK(rtl_rsn_parse(ie,22,&r)!=0&&!r.psk);
        ie[i]=ie[i+1]=0;CHECK(rtl_rsn_parse(ie,22,&r)!=0&&!r.psk);
    }
    memcpy(ie,rsn_psk,22);ie[19]=1;
    CHECK(rtl_rsn_parse(ie,22,&r)==0&&!r.psk); /* enterprise */
    ie[19]=8;CHECK(rtl_rsn_parse(ie,22,&r)==0&&!r.psk); /* SAE only */
    ie[19]=2;ie[16]=1;CHECK(rtl_rsn_parse(ie,22,&r)==0&&!r.psk); /* foreign OUI */
    memcpy(ie,rsn_psk,22);ie[7]=2;
    CHECK(rtl_rsn_parse(ie,22,&r)==0&&!r.group_ccmp&&r.pairwise_ccmp&&r.psk);
    ie[7]=4;ie[13]=2;CHECK(rtl_rsn_parse(ie,22,&r)==0&&!r.pairwise_ccmp);
    /* Mixed selectors: accept only the exact RSN OUI/type, anywhere in lists. */
    memcpy(ie,rsn_psk,14);ie[8]=2;ie[13]=2;
    memcpy(ie+14,rsn_psk+10,4);memcpy(ie+18,rsn_psk+14,8);ie[1]=24;
    CHECK(rtl_rsn_parse(ie,26,&r)==0&&r.pairwise_ccmp&&r.psk);
    memcpy(ie,rsn_psk,22);ie[20]=0x40;
    CHECK(rtl_rsn_parse(ie,22,&r)!=0&&!r.psk); /* incoherent PMF */
    ie[20]=0xc0;CHECK(rtl_rsn_parse(ie,22,&r)==0&&r.capabilities==0xc0);
    /* PMKID list and optional management cipher must be completely bounded. */
    memcpy(ie,rsn_psk,22);ie[1]=42;ie[22]=1;ie[23]=0;memset(ie+24,0,16);
    ie[40]=0;ie[41]=15;ie[42]=172;ie[43]=6;
    CHECK(rtl_rsn_parse(ie,44,&r)==0&&r.psk);
    for(i=23;i<44;++i){
        ie[1]=(rtl_u8)(i-2);n=rtl_rsn_parse(ie,i,&r)==0;CHECK(n==(i==40));
    }
    ie[1]=42;ie[22]=255;ie[23]=255;CHECK(rtl_rsn_parse(ie,44,&r)!=0&&!r.psk);
    fresh(&t);len=fixture(f);CHECK(rtl_bss_observe(&t,f,len,11)==0);
    CHECK(t.count==1&&t.accepted==1&&!t.rejected);
    CHECK(t.entries[0].channel==11&&t.entries[0].received_channel==11);
    CHECK(t.entries[0].ssid_length==5&&!memcmp(t.entries[0].ssid,"NAEBA",5));
    CHECK(rtl_bss_advertises_psk_ccmp(&t.entries[0])&&!t.entries[0].hidden);
    CHECK(t.entries[0].rsn_length==22&&!memcmp(t.entries[0].rsn_ie,rsn_psk,22));
    CHECK(rtl_bss_observe(&t,f,len,10)==0&&t.count==1&&t.entries[0].observations==2);
    CHECK(t.entries[0].channel==11&&t.entries[0].received_channel==10);
    /* Replacing a BSSID never merges security from old observations. */
    CHECK(rtl_bss_observe(&t,f,sizeof(base),11)==0&&!rtl_bss_advertises_psk_ccmp(&t.entries[0]));
    CHECK(t.entries[0].rsn.status==RTL_RSN_ABSENT);
    len=fixture(f);f[len++]=48;f[len++]=0;
    CHECK(rtl_bss_observe(&t,f,len,11)==0&&t.entries[0].rsn.status==RTL_RSN_MALFORMED);
    CHECK(!rtl_bss_advertises_psk_ccmp(&t.entries[0])&&!t.entries[0].rsn_length);
    len=fixture(f);f[sizeof(base)+8]=255;
    CHECK(rtl_bss_observe(&t,f,len,11)==0&&t.entries[0].rsn.status==RTL_RSN_MALFORMED);
    /* Hidden/binary SSIDs are byte strings, including embedded NUL. */
    len=fixture(f);memset(f+38,0,5);
    CHECK(rtl_bss_observe(&t,f,len,11)==0&&t.entries[0].hidden);
    f[39]=255;CHECK(rtl_bss_observe(&t,f,len,11)==0&&!t.entries[0].hidden&&t.entries[0].ssid[1]==255);
    memcpy(f,base,36);f[36]=0;f[37]=0;
    CHECK(rtl_bss_observe(&t,f,38,11)==0&&t.entries[0].hidden&&!t.entries[0].channel);
    f[37]=32;memset(f+38,0xfe,32);
    CHECK(rtl_bss_observe(&t,f,70,11)==0&&t.entries[0].ssid_length==32);
    f[37]=33;CHECK(rtl_bss_observe(&t,f,71,11)!=0);
    /* Duplicate singleton and contradictory/short channel IEs are rejected. */
    len=fixture(f);f[len]=0;f[len+1]=0;CHECK(rtl_bss_observe(&t,f,len+2,11)!=0);
    f[len]=3;f[len+1]=1;f[len+2]=11;CHECK(rtl_bss_observe(&t,f,len+3,11)!=0);
    f[len]=61;f[len+1]=22;memset(f+len+2,0,22);f[len+2]=6;
    CHECK(rtl_bss_observe(&t,f,len+24,11)!=0);
    f[len+2]=11;CHECK(rtl_bss_observe(&t,f,len+24,11)==0);
    f[len+1]=1;CHECK(rtl_bss_observe(&t,f,len+3,11)!=0);
    len=fixture(f);f[45]=0;CHECK(rtl_bss_observe(&t,f,len,11)!=0);
    for(i=0;i<len;++i){
        fixture(f);fresh(&t);rtl_bss_observe(&t,f,i,11);
        CHECK(t.count<=1);if(t.count)CHECK(!rtl_bss_advertises_psk_ccmp(&t.entries[0]));
    }
    /* Never accept fragmented, non-beacon, group/zero BSSID, mismatched TA. */
    for(i=0;i<6;++i){
        len=fixture(f);fresh(&t);
        if(i==0)f[0]=0x50;if(i==1)f[1]=4;if(i==2)f[22]=1;
        if(i==3)f[16]=3;if(i==4)memset(f+10,0,12);if(i==5)f[10]=4;
        CHECK(rtl_bss_observe(&t,f,len,11)!=0&&!t.count);
    }
    fresh(&t);len=fixture(f);
    for(i=0;i<RTL_BSS_MAX+3;++i){
        f[15]=f[21]=(rtl_u8)i;CHECK(rtl_bss_observe(&t,f,len,11)==0);
    }
    CHECK(t.count==RTL_BSS_MAX&&t.dropped==3);
    f[15]=f[21]=0;CHECK(rtl_bss_observe(&t,f,len,11)==0&&t.entries[0].observations==2);
    /* End-to-end: RX verifies CRC before delivering, strips FCS, supports an
     * aggregate's valid prefix, never delivers partial/CRC-failed packets. */
    memset(usb,0,sizeof(usb));usb[0]=sizeof(verified);usb[10]=1;
    memcpy(usb+24,verified,sizeof(verified));fresh(&t);memset(&stats,0,sizeof(stats));
    CHECK(rtl8188cu_rx_parse_beacons(usb,24+sizeof(verified),&stats,sink,&t)==0);
    CHECK(t.count==1&&t.accepted==1&&t.entries[0].ssid_length==2&&stats.beacons==1);
    for(i=0;i<24+sizeof(verified);++i){
        fresh(&t);memset(&stats,0,sizeof(stats));
        CHECK(rtl8188cu_rx_parse_beacons(usb,i,&stats,sink,&t)!=0&&!t.accepted);
    }
    usb[24+38]^=1;fresh(&t);memset(&stats,0,sizeof(stats));
    CHECK(rtl8188cu_rx_parse_beacons(usb,24+sizeof(verified),&stats,sink,&t)==0&&!t.accepted&&stats.fcs_errors==1);
    usb[24+38]^=1;usb[1]=0x40;fresh(&t);memset(&stats,0,sizeof(stats));
    CHECK(rtl8188cu_rx_parse_beacons(usb,24+sizeof(verified),&stats,sink,&t)==0&&!t.accepted&&stats.crc_errors==1);
    usb[1]=0;usb[10]=2;fresh(&t);memset(&stats,0,sizeof(stats));
    CHECK(rtl8188cu_rx_parse_beacons(usb,129,&stats,sink,&t)==RTL_RX_MALFORMED&&t.accepted==1);
    /* Deterministic arbitrary data plus mutations of a structurally valid
     * beacon; useful on both ASan/UBSan host and big-endian GCC 3.3. */
    for(i=0;i<10000;++i){
        seed=seed*1664525U+1013904223U;n=seed%258;
        for(j=0;j<n;++j){seed=seed*1664525U+1013904223U;ie[j]=(rtl_u8)(seed>>24);}
        rtl_rsn_parse(ie,n,&r);CHECK(r.status!=RTL_RSN_VALID||n>=4);
        len=fixture(f);seed=seed*1664525U+1013904223U;f[seed%len]^=(rtl_u8)(seed>>24);
        fresh(&t);rtl_bss_observe(&t,f,len,11);CHECK(t.count<=1);
        seed=seed*1664525U+1013904223U;n=seed%sizeof(f);
        for(j=0;j<n;++j){seed=seed*1664525U+1013904223U;f[j]=(rtl_u8)(seed>>24);}
        fresh(&t);rtl_bss_observe(&t,f,n,11);CHECK(t.count<=1);
    }
    CHECK(rtl_rsn_parse(0,0,&r)!=0&&!r.psk);
    CHECK(rtl_rsn_parse(rsn_psk,22,0)==RTL_BAD_ARGUMENT);
    CHECK(rtl_bss_observe(0,base,sizeof(base),11)==RTL_BAD_ARGUMENT);
    puts("PASS: beacon/RSN bounds, PSK/CCMP/PMF selectors, binary SSIDs, channel consistency, bounded BSSID updates, verified-RX delivery, 10000 mutation/random trials");
    return 0;
}
