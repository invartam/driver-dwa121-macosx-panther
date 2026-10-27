#include "../config/config.h"
#include "../supplicant/wpa2.h"
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc,char **argv)
{
    struct dwa_config c;struct rlimit limit;char *secret=0,*end=0;unsigned long channel;int ok=0;
    limit.rlim_cur=limit.rlim_max=0;
    if(setrlimit(RLIMIT_CORE,&limit)||geteuid()!=0)return 1;
    memset(&c,0,sizeof(c));
    if(argc==2&&!strcmp(argv[1],"--channel")){
        ok=dwa_config_read(DWA_CONFIG_PATH,&c);if(ok)printf("%u\n",c.channel);
        dwa_config_clear(&c,sizeof(c));return ok?0:1;
    }
    if(argc!=3||!strlen(argv[1])||strlen(argv[1])>32||!isatty(STDIN_FILENO)){
        fprintf(stderr,"Usage: sudo dwa121-config SSID CHANNEL (1..11, WPA2-PSK/AES)\n");return 1;
    }
    channel=strtoul(argv[2],&end,10);if(!end||*end||channel<1||channel>11)return 1;
    memcpy(c.magic,"DWA121\001",8);c.channel=channel;c.ssid_length=strlen(argv[1]);memcpy(c.ssid,argv[1],c.ssid_length);
    secret=getpass("Mot de passe WPA2 (8..63 caracteres, saisie masquee) : ");
    if(secret&&strlen(secret)>=8&&strlen(secret)<=63&&!dwa_wpa2_pmk(secret,c.ssid,c.ssid_length,c.pmk))ok=dwa_config_write(DWA_CONFIG_PATH,&c);
    if(secret)dwa_config_clear(secret,strlen(secret));dwa_config_clear(&c,sizeof(c));
    puts(ok?"Configuration enregistree (cle derivee, acces root uniquement).":"Echec de la configuration.");return ok?0:1;
}
