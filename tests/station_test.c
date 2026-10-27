#include "../src/net80211/station.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const unsigned char mac[6]={2,1,2,3,4,5},ap[6]={2,6,7,8,9,10};
static unsigned int beacon(unsigned char *f)
{
    unsigned int n=36;
    memset(f,0,512);f[0]=0x80;memset(f+4,255,6);
    memcpy(f+10,ap,6);memcpy(f+16,ap,6);f[32]=100;f[34]=0x11;
    f[n++]=0;f[n++]=4;memcpy(f+n,"TEST",4);n+=4;
    f[n++]=3;f[n++]=1;f[n++]=11;
    f[n++]=1;f[n++]=4;f[n++]=0x82;f[n++]=0x84;f[n++]=0x8b;f[n++]=0x96;
    memcpy(f+n,rtl_station_rsn,22);return n+22;
}
static void response(unsigned char *f,unsigned int subtype)
{
    memset(f,0,512);f[0]=(unsigned char)subtype;
    memcpy(f+4,mac,6);memcpy(f+10,ap,6);memcpy(f+16,ap,6);
}
int main(void)
{
    struct rtl_station s,copy;unsigned char b[512],f[512];unsigned int n=beacon(b),i;
    assert(rtl_station_begin(&s,mac,b,n,(const unsigned char *)"TEST",4,11,0)==1);
    assert(s.state==RTL_STA_AUTH&&s.tx_length==30&&s.tx[26]==1);
    assert(!memcmp(s.tx+4,ap,6)&&!memcmp(s.tx+10,mac,6));
    copy=s;assert(rtl_station_timeout(&s)==1&&s.tx[1]==8);
    assert(!memcmp(s.tx+22,copy.tx+22,2));
    assert(rtl_station_timeout(&s)==1);assert(rtl_station_timeout(&s)<0&&s.state==RTL_STA_FAILED);
    s=copy;response(f,0xb0);f[26]=2;
    f[4]^=2;assert(rtl_station_receive(&s,f,30)<0&&s.state==RTL_STA_AUTH);f[4]^=2;
    f[28]=13;assert(rtl_station_receive(&s,f,30)<0&&s.state==RTL_STA_FAILED);f[28]=0;s=copy;
    f[30]=221;f[31]=9;memset(f+32,0,9);
    assert(rtl_station_receive(&s,f,31)<0&&s.state==RTL_STA_AUTH);
    assert(rtl_station_receive(&s,f,40)<0&&s.state==RTL_STA_AUTH);
    assert(rtl_station_receive(&s,f,41)==1&&s.state==RTL_STA_ASSOC);s=copy;
    f[31]=255;assert(rtl_station_receive(&s,f,41)<0&&s.state==RTL_STA_AUTH);
    response(f,0xb0);f[26]=2;
    assert(rtl_station_receive(&s,f,30)==1&&s.state==RTL_STA_ASSOC);
    assert(!memcmp(s.tx+s.tx_length-22,rtl_station_rsn,22));
    assert(s.tx[24]==0x11&&s.tx[26]==10);copy=s;
    response(f,0x10);f[24]=0x11;f[28]=1;f[29]=0xc0;
    f[30]=1;f[31]=4;f[32]=0x82;f[33]=0x84;f[34]=0x8b;f[35]=0x96;
    assert(rtl_station_receive(&s,f,35)<0&&s.state==RTL_STA_ASSOC);
    assert(rtl_station_receive(&s,f,36)==0&&s.state==RTL_STA_KEYS&&s.aid==1);
    assert(rtl_station_keys_ready(&s,1)==0&&s.state==RTL_STA_AUTHORIZED);
    response(f,0xc0);f[24]=3;assert(rtl_station_receive(&s,f,26)<0&&s.state==RTL_STA_FAILED);
    s=copy;response(f,0x10);f[26]=17;assert(rtl_station_receive(&s,f,30)<0);
    assert(rtl_station_begin(&s,mac,b,n,(const unsigned char *)"FAIL",4,11,0)<0);
    assert(rtl_station_begin(&s,mac,b,n,(const unsigned char *)"TEST",4,11,5001)<0);
    assert(rtl_station_begin(&s,mac,b,n,(const unsigned char *)"TEST",4,1,0)<0);
    b[n-2]=0xc0;assert(rtl_station_begin(&s,mac,b,n,(const unsigned char *)"TEST",4,11,0)<0);b[n-2]=0;
    b[47]=0xff;assert(rtl_station_begin(&s,mac,b,n,(const unsigned char *)"TEST",4,11,0)<0);
    n=beacon(b);for(i=0;i<n;i++)assert(rtl_station_begin(&s,mac,b,i,(const unsigned char *)"TEST",4,11,0)<0);
    rtl_station_disconnect(&s);assert(s.state==RTL_STA_IDLE&&s.tx_length==0);
    puts("PASS: station auth/association, negotiated RSN, addresses, rates, retries, freshness, PMF refusal, truncations, controlled-port gate");
    return 0;
}
