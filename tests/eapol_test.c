#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/net80211/eapol.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static const rtl_u8 mac[]={2,1,2,3,4,5},ap[]={2,6,7,8,9,10};
static unsigned int make(rtl_u8 *f,unsigned int qos,unsigned int kd)
{
    static const rtl_u8 llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    unsigned int h=qos?26:24;rtl_u8 *e=f+h+8;
    memset(f,0,2083);f[0]=(rtl_u8)(qos?0x88:8);f[1]=2;
    memcpy(f+4,mac,6);memcpy(f+10,ap,6);memcpy(f+16,ap,6);memcpy(f+h,llc,8);
    e[0]=2;e[1]=3;e[2]=(rtl_u8)((95+kd)>>8);e[3]=(rtl_u8)(95+kd);e[4]=2;
    e[6]=0x8a;e[8]=16;e[16]=1;memset(e+17,0x11,32);
    e[97]=(rtl_u8)(kd>>8);e[98]=(rtl_u8)kd;return h+8+99+kd;
}
static void rejected(const rtl_u8 *f,unsigned int n)
{
    struct rtl_eapol_view v;memset(&v,0xa5,sizeof(v));
    CHECK(rtl_eapol_from_80211(f,n,mac,ap,&v)!=0);
    CHECK(!v.data&&!v.length&&!v.key_info&&!v.key_data_length&&!v.is_m1);
}
int main(void)
{
    rtl_u8 f[2083],copy[2083];struct rtl_eapol_view v;
    unsigned int q,n,h,i,j,seed=42;
    for(q=0;q<2;++q) {
        n=make(f,q,0);h=q?34:32;
        CHECK(rtl_eapol_from_80211(f,n,mac,ap,&v)==0&&v.is_m1&&v.data==f+h&&v.length==99);
        for(i=0;i<n;++i)rejected(f,i);
        rejected(f,n+1);
        for(i=0;i<15;++i) {
            memcpy(copy,f,n);
            if(i<3)copy[4+i*6]^=2;
            if(i==3)copy[1]|=0x40;
            if(i==4)copy[1]|=4;
            if(i==5)copy[22]|=1;
            if(i==6)copy[1]|=0x80;
            if(i==7)copy[1]=1;
            if(i==8)copy[h-1]=0;
            if(i==9)copy[h]=3;
            if(i==10)copy[h+1]=0;
            if(i==11)copy[h+3]++;
            if(i==12)copy[h+4]=254;
            if(i==13)copy[h+6]=0x89;
            if(i==14)copy[h+98]=1;
            rejected(copy,n);
        }
        if(q){f[24]=0x80;rejected(f,n);f[24]=0;}
        f[1]|=8;CHECK(rtl_eapol_from_80211(f,n,mac,ap,&v)==0&&v.is_m1);
        memset(f+h+17,0,32);CHECK(rtl_eapol_from_80211(f,n,mac,ap,&v)==0&&!v.is_m1);
        n=make(f,q,22);f[h+99]=221;f[h+100]=20;
        CHECK(rtl_eapol_from_80211(f,n,mac,ap,&v)==0&&v.is_m1&&v.key_data_length==22);
        f[h+100]=21;CHECK(rtl_eapol_from_80211(f,n,mac,ap,&v)==0&&!v.is_m1);
        n=make(f,q,1949);CHECK(rtl_eapol_from_80211(f,n,mac,ap,&v)==0&&v.length==2048);
        n=make(f,q,1950);rejected(f,n);
    }
    rejected(0,100);n=make(f,0,0);
    CHECK(rtl_eapol_from_80211(f,n,0,ap,&v)!=0&&!v.data);
    CHECK(rtl_eapol_from_80211(f,n,mac,0,&v)!=0&&!v.data);
    CHECK(rtl_eapol_from_80211(f,n,mac,ap,0)!=0);
    for(i=0;i<10000;++i) {
        seed=seed*1664525U+1013904223U;n=seed%sizeof(f);
        for(j=0;j<n;++j){seed=seed*1664525U+1013904223U;f[j]=(rtl_u8)(seed>>24);}
        rtl_eapol_from_80211(f,n,mac,ap,&v);
        if(v.data)CHECK(v.data>=f&&v.data+v.length<=f+n);
    }
    puts("PASS: EAPOL extraction, M1 syntax, optional key data, QoS, bounds, peer/fragment/protection filters, output wipe and 10000 fuzz cases");return 0;
}
