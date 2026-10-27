#include "../src/config/config.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    struct dwa_config c;unsigned int i;
    memset(&c,0,sizeof(c));memcpy(c.magic,"DWA121\001",8);c.channel=11;c.ssid_length=4;memcpy(c.ssid,"test",4);
    assert(sizeof(c)==76&&dwa_config_valid(&c));
    c.reserved[0]=1;assert(!dwa_config_valid(&c));c.reserved[0]=0;
    c.ssid[4]=1;assert(!dwa_config_valid(&c));c.ssid[4]=0;
    c.ssid[1]=0;assert(!dwa_config_valid(&c));c.ssid[1]='e';
    for(i=0;i<256;++i){c.channel=i;assert(dwa_config_valid(&c)==(i>=1&&i<=11));}
    c.channel=1;
    for(i=0;i<256;++i){c.ssid_length=i;assert(dwa_config_valid(&c)==(i==4));}
    dwa_config_clear(&c,sizeof(c));for(i=0;i<sizeof(c);++i)assert(!((unsigned char *)&c)[i]);
    puts("PASS: portable credential format, strict channel/SSID/padding and secret wipe");return 0;
}
