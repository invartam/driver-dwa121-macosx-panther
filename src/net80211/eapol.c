#include "eapol.h"
static unsigned int be16(const rtl_u8 *p)
{ return ((unsigned int)p[0]<<8)|p[1]; }
static int same(const rtl_u8 *a,const rtl_u8 *b,unsigned int n)
{ unsigned int i; for(i=0;i<n;++i) if(a[i]!=b[i]) return 0; return 1; }
static int zero(const rtl_u8 *p,unsigned int n)
{ unsigned int i; for(i=0;i<n;++i) if(p[i]) return 0; return 1; }
int rtl_eapol_from_80211(const rtl_u8 *f,unsigned int n,const rtl_u8 mac[6],
                       const rtl_u8 ap[6],struct rtl_eapol_view *v)
{
    static const rtl_u8 llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    const rtl_u8 *e; unsigned int h,len,pos,kd,m1;
    if(!v) return RTL_BAD_ARGUMENT;
    v->data=0; v->length=0; v->key_info=0; v->key_data_length=0; v->is_m1=0;
    if(!f || !mac || !ap || n<24 || n>2082 || (mac[0]&1) || (ap[0]&1) ||
       zero(mac,6) || zero(ap,6) || same(mac,ap,6) ||
       (f[0]!=8 && f[0]!=0x88) || (f[1]&0xc7)!=2 || (f[22]&15) ||
       !same(f+4,mac,6) || !same(f+10,ap,6) || !same(f+16,ap,6)) return RTL_BAD_ARGUMENT;
    h=f[0]==0x88 ? 26 : 24;
    if(n<h+8+99 || (h==26 && (f[24]&0x80)) || !same(f+h,llc,8)) return RTL_BAD_ARGUMENT;
    e=f+h+8; len=n-h-8;
    if(len>2048 || (e[0]!=1 && e[0]!=2) || e[1]!=3 || e[4]!=2 ||
       be16(e+2)!=len-4 || (be16(e+5)&7)!=2) return RTL_BAD_ARGUMENT;
    kd=be16(e+97);
    if(kd!=len-99) return RTL_BAD_ARGUMENT;
    m1=be16(e+5)==0x008a && be16(e+7)==16 && !zero(e+17,32) && zero(e+49,48);
    /* M1 key data may contain a PMKID KDE. Unknown complete IEs are left
     * to the supplicant; truncated IEs cannot be a compatible M1. */
    if(m1) for(pos=99;pos<len;) {
        if(len-pos<2 || e[pos+1]>len-pos-2) { m1=0; break; }
        pos+=2+e[pos+1];
    }
    v->data=e; v->length=len; v->key_info=be16(e+5); v->key_data_length=kd; v->is_m1=m1;
    return RTL_OK;
}
