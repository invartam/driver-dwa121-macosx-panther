/* Userland Ethernet/CCMP boundary. MIT, except CCMP AAD/nonce layout follows
 * OpenBSD ieee80211_crypto_ccmp.c, whose notice is retained below:
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "utils/includes.h"
#include "utils/common.h"
#include "crypto/aes_wrap.h"
#include "data.h"
#include "data_pn.h"
struct data_key {
    unsigned char key[16],rx[16][6];
    int installed;
};
struct dwa_data {
    unsigned char mac[6],ap[6],tx[6];
    struct data_key keys[5]; /* GTK 0..3; PTK 4. */
    unsigned int sequence;
    int authorized,dead;
};
/* hostap CCM accesses aligned words and writes 16 bytes for a partial last
 * block. Never pass the caller's exact-sized/unaligned buffer to that API. */
union ccm_buffer { u32 align; unsigned char bytes[1520]; };
static const unsigned char snap[6]={0xaa,0xaa,3,0,0,0};
static void wipe(void *p,size_t n)
{ volatile unsigned char *v=p;while(n--)*v++=0; }
static int unicast(const unsigned char *a)
{
    static const unsigned char zero[6]={0};
    return a&&!(a[0]&1)&&memcmp(a,zero,6);
}
static int protocol(const unsigned char *p)
{
    unsigned int t=((unsigned int)p[0]<<8)|p[1];
    return t>=0x600&&t!=0x888e&&t!=0x80f3&&t!=0x8137;
}
struct dwa_data *dwa_data_create(const unsigned char mac[6],const unsigned char ap[6])
{
    struct dwa_data *d;
    if(!unicast(mac)||!unicast(ap)||!memcmp(mac,ap,6))return NULL;
    d=calloc(1,sizeof(*d));if(!d)return NULL;
    memcpy(d->mac,mac,6);memcpy(d->ap,ap,6);return d;
}
void dwa_data_clear(void *cookie)
{
    struct dwa_data *d=cookie;unsigned int i;
    if(!d)return;
    /* WPA closes/clears an empty port during create/start as well. Once any
     * key exists, clearing is terminal so no key can restart a PN at zero. */
    if(!d->dead) {
        for(i=0;i<5&&!d->keys[i].installed;++i){}
        if(i==5)return;
    }
    wipe(d,sizeof(*d));d->dead=1;
}
void dwa_data_destroy(struct dwa_data *d)
{ if(d){wipe(d,sizeof(*d));free(d);} }
int dwa_data_authorized(const struct dwa_data *d)
{ return d&&!d->dead&&d->authorized; }
int dwa_data_port(void *cookie,int enabled)
{
    struct dwa_data *d=cookie;unsigned int i;
    if(!d|| (enabled!=0&&enabled!=1))return -1;
    if(!enabled){dwa_data_clear(d);return 0;}
    if(d->dead||!d->keys[4].installed)return -1;
    for(i=0;i<4;++i)if(d->keys[i].installed){d->authorized=1;return 0;}
    return -1;
}
int dwa_data_install_ccmp(void *cookie,int pair,unsigned int index,
    const unsigned char key[16],const unsigned char *rsc,size_t rn)
{
    struct dwa_data *d=cookie;struct data_key *k;unsigned int slot,i;
    if(!d||d->dead||!key||(pair!=0&&pair!=1)||index>3||(pair&&index)||
       rn>6||(rn&&!rsc))return -1;
    slot=pair?4:index;k=&d->keys[slot];
    if(k->installed)return memcmp(k->key,key,16)?-1:0;
    for(i=0;i<5;++i)if(d->keys[i].installed&&!memcmp(d->keys[i].key,key,16))return -1;
    memcpy(k->key,key,16);
    for(i=0;i<16;++i)if(rn)memcpy(k->rx[i],rsc,rn);
    k->installed=1;return 0;
}
static size_t parameters(const unsigned char *f,int qos,const unsigned char pn[6],
    unsigned char aad[24],unsigned char nonce[13])
{
    unsigned int i;
    aad[0]=f[0]&0x8f;aad[1]=f[1]&0xc7;
    memcpy(aad+2,f+4,18);aad[20]=f[22]&15;aad[21]=0;
    nonce[0]=qos?(f[24]&15):0;memcpy(nonce+1,f+10,6);
    for(i=0;i<6;++i)nonce[7+i]=pn[5-i];
    if(qos){aad[22]=nonce[0];aad[23]=0;return 24;}
    return 22;
}
int dwa_data_encrypt(struct dwa_data *d,const unsigned char *e,size_t n,
    unsigned char *out,size_t capacity,size_t *length)
{
    union ccm_buffer plain,crypt;unsigned char f[DWA_DATA_WIFI_MAX],aad[24],nonce[13];
    size_t payload,an;int rc=-1;
    if(length)*length=0;
    if(!length||!dwa_data_authorized(d)||!e||!out||n<14||n>DWA_DATA_ETH_MAX||
       memcmp(e+6,d->mac,6)||!protocol(e+12))return -1;
    payload=n-6; /* 8-byte LLC replaces Ethernet's 14-byte header. */
    if(capacity<24+8+payload+8)return -1;
    if(dwa_pn_next(d->tx)){dwa_data_clear(d);return -1;}
    memset(f,0,32);f[0]=8;f[1]=0x41;
    memcpy(f+4,d->ap,6);memcpy(f+10,d->mac,6);memcpy(f+16,e,6);
    f[22]=(unsigned char)(d->sequence<<4);f[23]=(unsigned char)(d->sequence>>4);
    d->sequence=(d->sequence+1)&4095;
    f[24]=d->tx[0];f[25]=d->tx[1];f[27]=0x20;memcpy(f+28,d->tx+2,4);
    memcpy(plain.bytes,snap,6);memcpy(plain.bytes+6,e+12,n-12);
    an=parameters(f,0,d->tx,aad,nonce);
    if(!aes_ccm_ae(d->keys[4].key,16,nonce,8,plain.bytes,payload,aad,an,
       crypt.bytes,f+32+payload)) {
        memcpy(f+32,crypt.bytes,payload);memcpy(out,f,40+payload);
        *length=40+payload;rc=0;
    } /* PN was consumed even if encryption fails or caller abandons TX. */
    wipe(&plain,sizeof(plain));wipe(&crypt,sizeof(crypt));wipe(f,sizeof(f));return rc;
}
static int decrypt(struct dwa_data *d,const unsigned char *f,size_t n,
    unsigned char *out,size_t capacity,size_t *length,int control)
{
    union ccm_buffer plain,crypt;struct data_key *k;
    unsigned char aad[24],nonce[13],pn[6];const unsigned char *cc;
    size_t h,payload,an;unsigned int slot,tid;int qos,rc=-1;
    if(length)*length=0;
    if(!length||!dwa_data_authorized(d)||!f||!out||n<48||n>DWA_DATA_WIFI_MAX||
       (f[0]!=8&&f[0]!=0x88)||(f[1]&0xc7)!=0x42||(f[22]&15)||
       memcmp(f+10,d->ap,6)||!unicast(f+16))return -1;
    /* Non-QoS uses priority zero in the nonce and shares TID 0's replay
     * counter. Separate counters would accept the same key/nonce twice. */
    qos=f[0]==0x88;h=qos?26:24;tid=qos?(f[24]&15):0;
    if((qos&&((f[24]&0x80)||f[25]))||n<h+24)return -1;
    cc=f+h;if(cc[2]||(cc[3]&0x3f)!=0x20)return -1;
    slot=cc[3]>>6;
    if(!(f[4]&1)) {
        if(memcmp(f+4,d->mac,6)||slot)return -1;
        slot=4;
    }
    k=&d->keys[slot];if(!k->installed)return -1;
    pn[0]=cc[0];pn[1]=cc[1];memcpy(pn+2,cc+4,4);
    if(!dwa_pn_greater(pn,k->rx[tid]))return -1;
    payload=n-h-16;if(payload>1508||capacity<payload+6)return -1;
    an=parameters(f,qos,pn,aad,nonce);memcpy(crypt.bytes,cc+8,payload);
    if(!aes_ccm_ad(k->key,16,nonce,8,crypt.bytes,payload,aad,an,
       f+n-8,plain.bytes)&&!memcmp(plain.bytes,snap,6)&&
       (protocol(plain.bytes+6)||(control&&plain.bytes[6]==0x88&&plain.bytes[7]==0x8e))) {
        memcpy(out,f+4,6);memcpy(out+6,f+16,6);memcpy(out+12,plain.bytes+6,payload-6);
        memcpy(k->rx[tid],pn,6);*length=payload+6;rc=0;
    }
    wipe(&plain,sizeof(plain));wipe(&crypt,sizeof(crypt));return rc;
}
int dwa_data_decrypt(struct dwa_data *d,const unsigned char *f,size_t n,
    unsigned char *out,size_t capacity,size_t *length)
{ return decrypt(d,f,n,out,capacity,length,0); }
int dwa_data_decrypt_network(struct dwa_data *d,const unsigned char *f,size_t n,
    unsigned char *out,size_t capacity,size_t *length)
{ return decrypt(d,f,n,out,capacity,length,1); }
