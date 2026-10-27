#include "dhcp_probe.h"
#include <string.h>
static unsigned int be16(const rtl_u8 *p){return ((unsigned int)p[0]<<8)|p[1];}
static void put16(rtl_u8 *p,unsigned int v){p[0]=(rtl_u8)(v>>8);p[1]=(rtl_u8)v;}
static rtl_u32 sum(const rtl_u8 *p,unsigned int n,rtl_u32 s)
{
    while(n>=2){s+=be16(p);p+=2;n-=2;}if(n)s+=(rtl_u32)*p<<8;
    while(s>>16)s=(s&65535)+(s>>16);return s;
}
static unsigned int udp_sum(const rtl_u8 *ip,const rtl_u8 *udp,unsigned int n)
{return (unsigned int)sum(udp,n,sum(ip+12,8,17+n));}
static int unicast_ip(const rtl_u8 *p)
{return p[0]&&p[0]!=127&&p[0]<224&&!(p[0]==255&&p[1]==255&&p[2]==255&&p[3]==255);}
int dwa_dhcp_discover(rtl_u8 *e,unsigned int cap,const rtl_u8 mac[6],const rtl_u8 xid[4],unsigned int seconds)
{
    rtl_u8 *ip,*udp,*b,*o;unsigned int check;
    if(!e||cap<DWA_DHCP_DISCOVER_SIZE||!mac||!xid||(mac[0]&1)||seconds>65535)return RTL_BAD_ARGUMENT;
    memset(e,0,DWA_DHCP_DISCOVER_SIZE);memset(e,255,6);memcpy(e+6,mac,6);e[12]=8;
    ip=e+14;udp=ip+20;b=udp+8;o=b+240;
    ip[0]=0x45;put16(ip+2,328);ip[8]=64;ip[9]=17;memset(ip+16,255,4);
    put16(ip+10,(unsigned int)(~sum(ip,20,0))&65535);
    put16(udp,68);put16(udp+2,67);put16(udp+4,308);
    b[0]=1;b[1]=1;b[2]=6;memcpy(b+4,xid,4);put16(b+8,seconds);b[10]=0x80;memcpy(b+28,mac,6);
    b[236]=99;b[237]=130;b[238]=83;b[239]=99;
    *o++=53;*o++=1;*o++=1; /* DHCPDISCOVER */
    *o++=61;*o++=7;*o++=1;memcpy(o,mac,6);o+=6; /* client identifier */
    *o++=55;*o++=3;*o++=1;*o++=3;*o++=6; /* mask, router, DNS */
    *o=255;check=(~udp_sum(ip,udp,308))&65535;put16(udp+6,check?check:65535);
    return RTL_OK;
}
int dwa_dhcp_parse_offer(const rtl_u8 *e,unsigned int n,const rtl_u8 mac[6],const rtl_u8 xid[4],struct dwa_dhcp_offer *out)
{
    static const rtl_u8 broadcast[6]={255,255,255,255,255,255};
    const rtl_u8 *ip,*udp,*b,*o;unsigned int total,un,left,code,len,types=0,servers=0,ended=0;
    struct dwa_dhcp_offer found;
    if(!out)return RTL_BAD_ARGUMENT;memset(out,0,sizeof(*out));memset(&found,0,sizeof(found));
    if(!e||!mac||!xid||n<14+20+8+240||n>1514||e[12]!=8||e[13]||
       (memcmp(e,mac,6)&&memcmp(e,broadcast,6))||(e[6]&1))return RTL_BAD_ARGUMENT;
    ip=e+14;total=be16(ip+2);
    if(ip[0]!=0x45||ip[9]!=17||(be16(ip+6)&0xbfff)||total<268||total>n-14||
       sum(ip,20,0)!=65535||!unicast_ip(ip+12))return RTL_BAD_ARGUMENT;
    udp=ip+20;un=be16(udp+4);
    if(be16(udp)!=67||be16(udp+2)!=68||un!=total-20||un<248||
       (be16(udp+6)&&udp_sum(ip,udp,un)!=65535))return RTL_BAD_ARGUMENT;
    b=udp+8;
    if(b[0]!=2||b[1]!=1||b[2]!=6||memcmp(b+4,xid,4)||memcmp(b+28,mac,6)||
       !unicast_ip(b+16)||memcmp(b+236,"\x63\x82\x53\x63",4)||
       (memcmp(ip+16,broadcast,4)&&memcmp(ip+16,b+16,4)))return RTL_BAD_ARGUMENT;
    memcpy(found.address,b+16,4);o=b+240;left=un-248;
    while(left) {
        code=*o++;--left;if(!code)continue;if(code==255){ended=1;break;}
        if(!left)return RTL_BAD_ARGUMENT;len=*o++;--left;if(len>left)return RTL_BAD_ARGUMENT;
        if(code==52)return RTL_UNSUPPORTED; /* option overload not supported */
        if(code==53){if(types++||len!=1||o[0]!=2)return RTL_BAD_ARGUMENT;}
        if(code==54){if(servers++||len!=4||!unicast_ip(o))return RTL_BAD_ARGUMENT;memcpy(found.server,o,4);}
        o+=len;left-=len;
    }
    if(!ended||types!=1||servers!=1)return RTL_BAD_ARGUMENT;
    *out=found;return RTL_OK;
}
