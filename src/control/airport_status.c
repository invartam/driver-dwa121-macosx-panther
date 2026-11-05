#include "airport_status.h"
#include <string.h>
int dwa_airport_status(const struct dwa_airport_target *t,const rtl_u8 *mac,rtl_u8 *out,unsigned int n)
{
    unsigned int i,nonzero=0;
    if(!out||n!=56||!t||!mac)return 0;
    memset(out,0,n);out[5]=1;memcpy(out+40,mac,6);
    if(t->phase==DWA_AP_FAILED||t->phase==DWA_AP_DISCONNECTED){out[55]=4;return 1;}
    if(t->phase!=DWA_AP_CONNECTED)return 1;
    if(t->version!=DWA_AIRPORT_VERSION||!t->generation||!t->ssid_length||t->ssid_length>=32||
       t->channel<1||t->channel>11||(t->bssid[0]&1))return 0;
    for(i=0;i<6;++i)nonzero|=t->bssid[i];
    if(!nonzero)return 0;
    for(i=0;i<t->ssid_length;++i)if(!t->ssid[i])return 0;
    out[3]=4;out[55]=1;memcpy(out+6,t->ssid,t->ssid_length);memcpy(out+46,t->bssid,6);
    return 1;
}
