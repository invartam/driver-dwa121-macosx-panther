#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../src/control/airport_status.h"
int main(void)
{
    struct dwa_airport_target t;unsigned char guarded[58],mac[6]={2,1,2,3,4,5};unsigned int phase,i;
    memset(&t,0,sizeof(t));t.version=1;t.generation=9;t.channel=11;t.ssid_length=5;
    memcpy(t.ssid,"NAEBA",5);memcpy(t.bssid,mac,6);t.bssid[5]=6;
    for(phase=0;phase<=6;++phase){
        memset(guarded,0xa5,sizeof(guarded));t.phase=phase;
        assert(dwa_airport_status(&t,mac,guarded+1,56));
        assert(guarded[0]==0xa5&&guarded[57]==0xa5);
        assert(guarded[3]==0&&guarded[5]==0&&guarded[6]==1);
        assert(!memcmp(guarded+41,mac,6));
        if(phase==DWA_AP_CONNECTED){assert(guarded[4]==4&&guarded[56]==1);assert(!memcmp(guarded+7,"NAEBA",5));assert(!memcmp(guarded+47,t.bssid,6));}
        else {assert(guarded[4]==0);for(i=7;i<41;++i)assert(!guarded[i]);for(i=47;i<53;++i)assert(!guarded[i]);assert(guarded[56]==(phase>=5?4:0));}
    }
    t.phase=DWA_AP_CONNECTED;t.generation=0;assert(!dwa_airport_status(&t,mac,guarded+1,56));t.generation=1;
    t.ssid_length=32;assert(!dwa_airport_status(&t,mac,guarded+1,56));t.ssid_length=5;
    t.ssid[2]=0;assert(!dwa_airport_status(&t,mac,guarded+1,56));t.ssid[2]='E';
    t.bssid[0]=1;assert(!dwa_airport_status(&t,mac,guarded+1,56));t.bssid[0]=2;
    t.channel=12;assert(!dwa_airport_status(&t,mac,guarded+1,56));t.channel=11;
    assert(!dwa_airport_status(&t,mac,guarded+1,55));assert(!dwa_airport_status(&t,mac,guarded+1,57));
    assert(!dwa_airport_status(0,mac,guarded+1,56));
    puts("PASS: native status bounds/BE, identity, no premature success, failed/disconnected invalidation");return 0;
}
