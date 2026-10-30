#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/net80211/scan.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    static struct rtl_bss_table t,s;
    unsigned char wire[52];unsigned int i;
    s.count=1;s.entries[0].ssid_length=3;memcpy(s.entries[0].ssid,"caf",3);
    s.entries[0].bssid[0]=2;s.entries[0].bssid[5]=9;
    s.entries[0].channel=11;s.entries[0].received_channel=10;
    s.entries[0].beacon_interval=100;s.entries[0].capabilities=0x1511;
    s.entries[0].last_seen_ms=1000;
    memset(wire,0xa5,sizeof(wire));CHECK(dwa_scan_native(&s.entries[0],wire+1));
    CHECK(wire[0]==0xa5&&wire[51]==0xa5);
    CHECK(wire[1]==0&&wire[2]==11&&wire[3]==0&&wire[4]==0&&wire[5]==0&&wire[6]==0);
    CHECK(wire[7]==2&&wire[12]==9&&wire[13]==0&&wire[14]==100);
    CHECK(wire[15]==0x15&&wire[16]==0x11&&wire[17]==0&&wire[18]==3);
    CHECK(!memcmp(wire+19,"caf",3)&&wire[22]==0);
    dwa_scan_merge(&t,&s,1000);dwa_scan_merge(&t,&s,1001);CHECK(t.count==1);
    s.entries[0].bssid[5]=10;dwa_scan_merge(&t,&s,1002);CHECK(t.count==2); /* same SSID, distinct BSSID */
    dwa_scan_expire(&t,15999);CHECK(t.count==2);dwa_scan_expire(&t,16000);CHECK(!t.count);
    s.entries[0].last_seen_ms=0xfffffff0U;dwa_scan_merge(&t,&s,20);CHECK(t.count==1);
    dwa_scan_expire(&t,15000);CHECK(!t.count); /* monotonic wrap */
    s.entries[0].last_seen_ms=20000;
    for(i=0;i<RTL_BSS_MAX+3;++i){s.entries[0].bssid[5]=(unsigned char)i;dwa_scan_merge(&t,&s,20001);}
    CHECK(t.count==RTL_BSS_MAX&&t.dropped==3);
    s.entries[0].ssid[1]=0xc3;s.entries[0].ssid[2]=0xa9;
    CHECK(dwa_scan_native(&s.entries[0],wire)&&wire[19]==0xc3&&wire[20]==0xa9);
    s.entries[0].ssid[1]=0;CHECK(!dwa_scan_native(&s.entries[0],wire));
    memset(s.entries[0].ssid,'x',32);s.entries[0].ssid_length=32;
    CHECK(!dwa_scan_native(&s.entries[0],wire));
    s.entries[0].ssid_length=0;CHECK(!dwa_scan_native(&s.entries[0],wire));
    s.entries[0].ssid_length=3;s.entries[0].hidden=1;CHECK(!dwa_scan_native(&s.entries[0],wire));
    s.entries[0].hidden=0;s.entries[0].ssid_length=33;CHECK(!dwa_scan_native(&s.entries[0],wire));
    memset(&t,0,sizeof(t));dwa_scan_merge(&t,&s,20001);CHECK(!t.count);
    puts("PASS: scan BSSID merge/capacity, freshness/wrap, BE unaligned wire/canaries, binary/hidden/full-length SSID policy");
    return 0;
}
