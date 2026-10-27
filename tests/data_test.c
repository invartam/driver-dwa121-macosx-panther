/* Independent CCM oracle: OpenSSL AES block primitive, not hostap CCM.
 * Synthetic keys only. RFC3610 section 8 packet vector 1 anchors both engines. */
#include "../src/supplicant/data.h"
#include "../src/supplicant/data_pn.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/aes.h>
#include "utils/includes.h"
#include "utils/common.h"
#include "crypto/aes_wrap.h"
static const unsigned char sta[6]={2,1,2,3,4,5},ap[6]={2,6,7,8,9,10};
static const unsigned char lan[6]={2,11,12,13,14,15};
static const unsigned char group[6]={1,0,0x5e,0,0,1};
static const unsigned char tk[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const unsigned char gtk[16]={16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
static void mac_block(const AES_KEY *key,unsigned char mac[16],const unsigned char *p)
{ unsigned int i;for(i=0;i<16;++i)mac[i]^=p[i];AES_encrypt(mac,mac,key); }
static void oracle(const unsigned char key[16],const unsigned char nonce[13],
    const unsigned char *aad,size_t an,const unsigned char *plain,size_t n,
    unsigned char *encrypted,unsigned char tag[8])
{
    AES_KEY aes;unsigned char mac[16]={0},b[16]={0},a[16]={0},stream[16];
    unsigned char auth[32]={0};size_t i,j,take;
    assert(an<=30&&n<=1508&&AES_set_encrypt_key(key,128,&aes)==0);
    b[0]=an?0x59:0x19;memcpy(b+1,nonce,13);b[14]=(unsigned char)(n>>8);b[15]=(unsigned char)n;
    mac_block(&aes,mac,b);
    if(an) {
        auth[0]=(unsigned char)(an>>8);auth[1]=(unsigned char)an;memcpy(auth+2,aad,an);
        for(i=0;i<an+2;i+=16)mac_block(&aes,mac,auth+i);
    }
    for(i=0;i<n;i+=16) {
        take=n-i;if(take>16)take=16;memset(b,0,16);memcpy(b,plain+i,take);mac_block(&aes,mac,b);
    }
    a[0]=1;memcpy(a+1,nonce,13);AES_encrypt(a,stream,&aes);
    for(i=0;i<8;++i)tag[i]=mac[i]^stream[i];
    for(i=0;i<n;i+=16) {
        size_t counter=i/16+1;a[14]=(unsigned char)(counter>>8);a[15]=(unsigned char)counter;
        AES_encrypt(a,stream,&aes);take=n-i;if(take>16)take=16;
        for(j=0;j<take;++j)encrypted[i+j]=plain[i+j]^stream[j];
    }
}
static void rfc_vector(void)
{
    const unsigned char nonce[13]={0,0,0,3,2,1,0,0xa0,0xa1,0xa2,0xa3,0xa4,0xa5};
    const unsigned char expected[31]={0x58,0x8c,0x97,0x9a,0x61,0xc6,0x63,0xd2,
        0xf0,0x66,0xd0,0xc2,0xc0,0xf9,0x89,0x80,0x6d,0x5f,0x6b,0x61,0xda,0xc3,0x84,
        0x17,0xe8,0xd1,0x2c,0xfd,0xf9,0x26,0xe0};
    unsigned char key[16],aad[8],o[31],tag[8];unsigned int i;
    union {u32 align;unsigned char b[32];} p,c,decoded;
    for(i=0;i<16;++i)key[i]=(unsigned char)(0xc0+i);
    for(i=0;i<8;++i)aad[i]=(unsigned char)i;
    for(i=0;i<23;++i)p.b[i]=(unsigned char)(i+8);
    oracle(key,nonce,aad,8,p.b,23,o,o+23);assert(!memcmp(o,expected,31));
    assert(!aes_ccm_ae(key,16,nonce,8,p.b,23,aad,8,c.b,tag));
    assert(!memcmp(c.b,expected,23)&&!memcmp(tag,expected+23,8));
    assert(!aes_ccm_ad(key,16,nonce,8,c.b,23,aad,8,tag,decoded.b)&&!memcmp(p.b,decoded.b,23));
}
/* Construct AAD by concatenating independent fields. No production helper. */
static void frame_oracle(unsigned char *f,const unsigned char key[16],
    const unsigned char *payload,size_t n)
{
    unsigned char aad[24]={0},nonce[13]={0};size_t h=f[0]==0x88?26:24,an=h-2;
    const unsigned char *cc=f+h;
    aad[0]=f[0];aad[1]=f[1]&~0x38;memcpy(aad+2,f+4,18);
    if(h==26){aad[22]=f[24]&15;nonce[0]=f[24]&15;}
    memcpy(nonce+1,f+10,6);
    nonce[7]=cc[7];nonce[8]=cc[6];nonce[9]=cc[5];nonce[10]=cc[4];nonce[11]=cc[1];nonce[12]=cc[0];
    oracle(key,nonce,aad,an,payload,n,f+h+8,f+h+8+n);
}
static size_t incoming(unsigned char *f,const unsigned char *eth,size_t n,int tid,
    unsigned int index,unsigned long long pn,const unsigned char key[16])
{
    unsigned char plain[1508];size_t h=tid<0?24:26;unsigned int i;
    memset(f,0,h+8);f[0]=tid<0?8:0x88;f[1]=0x42;
    memcpy(f+4,eth,6);memcpy(f+10,ap,6);memcpy(f+16,eth+6,6);
    if(tid>=0)f[24]=(unsigned char)tid;
    f[h]=(unsigned char)pn;f[h+1]=(unsigned char)(pn>>8);f[h+3]=(unsigned char)(0x20|(index<<6));
    for(i=0;i<4;++i)f[h+4+i]=(unsigned char)(pn>>(16+8*i));
    memcpy(plain,"\xaa\xaa\x03\x00\x00\x00",6);memcpy(plain+6,eth+12,n-12);
    frame_oracle(f,key,plain,n-6);return h+16+n-6;
}
static void ethernet(unsigned char *e,size_t n,int rx,int multicast)
{
    size_t i;assert(n>=14&&n<=1514);
    memcpy(e,multicast?group:(rx?sta:lan),6);memcpy(e+6,rx?lan:sta,6);
    e[12]=8;e[13]=0;for(i=14;i<n;++i)e[i]=(unsigned char)(i*17+3);
}
static struct dwa_data *ready(void)
{
    struct dwa_data *d=dwa_data_create(sta,ap);assert(d);
    assert(!dwa_data_install_ccmp(d,1,0,tk,NULL,0));
    assert(!dwa_data_install_ccmp(d,0,1,gtk,NULL,0));
    assert(!dwa_data_port(d,1));return d;
}
static void rejected(struct dwa_data *d,const unsigned char *f,size_t n)
{
    unsigned char out[1514];size_t len=123,i;memset(out,0x57,sizeof(out));
    assert(dwa_data_decrypt(d,f,n,out,sizeof(out),&len)<0&&len==0);
    for(i=0;i<sizeof(out);++i)assert(out[i]==0x57);
}
static void lengths(void)
{
    unsigned char ebuf[1516],fbuf[1552],outbuf[1516],ref[1550],plain[1508];
    unsigned char *e=ebuf+1,*f=fbuf+1,*out=outbuf+1;size_t n,fn,en;
    struct dwa_data *d=ready();unsigned long long pn=0;
    for(n=14;n<=1514;++n) {
        ethernet(e,n,0,n&1);memset(fbuf,0x5a,sizeof(fbuf));
        assert(!dwa_data_encrypt(d,e,n,f,n+34,&fn)&&fn==n+34);
        assert(fbuf[0]==0x5a&&fbuf[fn+1]==0x5a&&f[0]==8&&f[1]==0x41);
        assert(!memcmp(f+4,ap,6)&&!memcmp(f+10,sta,6)&&!memcmp(f+16,e,6));
        ++pn;assert(f[24]==(unsigned char)pn&&f[25]==(unsigned char)(pn>>8));
        memcpy(ref,f,32);memcpy(plain,"\xaa\xaa\x03\x00\x00\x00",6);memcpy(plain+6,e+12,n-12);
        frame_oracle(ref,tk,plain,n-6);assert(!memcmp(f,ref,fn));
        ethernet(e,n,1,n&1);fn=incoming(f,e,n,(int)(n%17)-1,n&1?1:0,pn,n&1?gtk:tk);
        memset(outbuf,0x6b,sizeof(outbuf));assert(!dwa_data_decrypt(d,f,fn,out,n,&en)&&en==n);
        assert(!memcmp(e,out,n)&&outbuf[0]==0x6b&&outbuf[n+1]==0x6b);
        rejected(d,f,fn);
    }
    dwa_data_destroy(d);
}
static void mutations(void)
{
    unsigned char e[64],f[128],b[128],out[1514],plain[58];size_t n,en,i;unsigned int bit;
    struct dwa_data *d;
    ethernet(e,sizeof(e),1,0);n=incoming(f,e,sizeof(e),7,0,0x010203040506ULL,tk);
    for(i=0;i<n;++i) {
        d=ready();rejected(d,f,i);assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en));dwa_data_destroy(d);
    }
    for(i=0;i<n;++i)for(bit=0;bit<8;++bit) {
        /* Duration/sequence and Retry/Power/MoreData/QoS ACK-policy are masked
         * by CCMP; test these separately instead of pretending they have MIC. */
        if(i==2||i==3||i==23||(i==22&&bit>=4)||(i==1&&bit>=3&&bit<=5)||
           (i==24&&bit>=4&&bit<=6))continue;
        memcpy(b,f,n);b[i]^=(unsigned char)(1U<<bit);d=ready();rejected(d,b,n);
        assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en)&&en==sizeof(e));dwa_data_destroy(d);
    }
    d=ready();memcpy(b,f,n);b[1]|=0x38;b[2]=99;b[22]=0xa0;b[23]=0x23;b[24]|=0x70;
    assert(!dwa_data_decrypt(d,b,n,out,sizeof(out),&en));rejected(d,f,n);dwa_data_destroy(d);
    /* Authenticated unsupported LLC/EAPOL must not escape the boundary. */
    memcpy(plain,"\xaa\xaa\x03\x00\x00\x00",6);memcpy(plain+6,e+12,52);
    d=ready();plain[6]=0x88;plain[7]=0x8e;memcpy(b,f,n);frame_oracle(b,tk,plain,58);rejected(d,b,n);
    plain[6]=8;plain[7]=0;plain[3]=1;frame_oracle(b,tk,plain,58);rejected(d,b,n);
    assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en));dwa_data_destroy(d);
}
static void counters_and_keys(void)
{
    unsigned char e[64],f[128],out[1514],rsc[6]={5,4,3,2,1,0},other[16],pn[6],old[6];
    size_t n,en;unsigned int i,j;struct dwa_data *d;
    memset(pn,255,6);memcpy(old,pn,6);assert(dwa_pn_next(pn)<0&&!memcmp(pn,old,6));
    assert(dwa_pn_next(pn)<0&&!memcmp(pn,old,6));
    for(i=0;i<6;++i) {
        memset(pn,255,6);pn[i]=254;memcpy(old,pn,6);assert(!dwa_pn_next(pn));
        assert(dwa_pn_greater(pn,old)&&!dwa_pn_greater(old,pn)&&!dwa_pn_greater(pn,pn));
        for(j=0;j<6;++j)assert(pn[j]==(j<i?0:255));
    }
    d=dwa_data_create(sta,ap);assert(d&&!dwa_data_port(d,0));dwa_data_clear(d);
    assert(dwa_data_port(d,1)<0);assert(!dwa_data_install_ccmp(d,1,0,tk,rsc,6));
    assert(dwa_data_port(d,1)<0);assert(!dwa_data_install_ccmp(d,0,1,gtk,rsc,6));
    assert(!dwa_data_port(d,1));ethernet(e,sizeof(e),1,1);
    for(i=0;i<16;++i) {
        n=incoming(f,e,sizeof(e),(int)i,1,0x0102030405ULL,gtk);rejected(d,f,n);
        n=incoming(f,e,sizeof(e),(int)i,1,0x0102030406ULL,gtk);
        assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en));rejected(d,f,n);
    }
    n=incoming(f,e,sizeof(e),-1,1,0x0102030406ULL,gtk);rejected(d,f,n);
    n=incoming(f,e,sizeof(e),-1,1,0x0102030407ULL,gtk);assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en));
    n=incoming(f,e,sizeof(e),0,1,0x0102030407ULL,gtk);rejected(d,f,n);
    /* Same GTK, lower RSC: never resets replay state. Higher RSC does not
     * overwrite it either; only a newly installed key consumes supplied RSC. */
    assert(!dwa_data_install_ccmp(d,0,1,gtk,NULL,0));rejected(d,f,n);
    memset(rsc,255,6);assert(!dwa_data_install_ccmp(d,0,1,gtk,rsc,6));
    n=incoming(f,e,sizeof(e),15,1,0x0102030407ULL,gtk);assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en));
    memcpy(other,gtk,16);other[0]^=1;assert(dwa_data_install_ccmp(d,0,1,other,NULL,0)<0);
    assert(dwa_data_install_ccmp(d,0,2,gtk,NULL,0)<0); /* no same key in another slot */
    assert(!dwa_data_install_ccmp(d,0,2,other,NULL,0));
    n=incoming(f,e,sizeof(e),15,2,1,other);assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en));
    n=incoming(f,e,sizeof(e),15,2,0xffffffffffffULL,other);assert(!dwa_data_decrypt(d,f,n,out,sizeof(out),&en));
    n=incoming(f,e,sizeof(e),15,2,1,other);rejected(d,f,n);
    assert(!dwa_data_port(d,0)&&!dwa_data_authorized(d));rejected(d,f,n);
    assert(dwa_data_port(d,1)<0&&dwa_data_install_ccmp(d,1,0,tk,NULL,0)<0);dwa_data_destroy(d);
    /* TX PN is reserved even if no send ever happens; duplicate PTK cannot
     * reset it. Sequence wraps at 4096, PN continues (including byte carry). */
    d=ready();ethernet(e,sizeof(e),0,0);
    for(i=1;i<=4100;++i) {
        assert(!dwa_data_encrypt(d,e,sizeof(e),f,sizeof(f),&n));
        assert(f[24]==(unsigned char)i&&f[25]==(unsigned char)(i>>8));
        assert((((unsigned int)f[23]<<4)|(f[22]>>4))==((i-1)&4095));
        assert(!dwa_data_install_ccmp(d,1,0,tk,NULL,0));
    }
    dwa_data_destroy(d);
}
static void bounds_and_random(void)
{
    struct dwa_data *d;unsigned char e[1515],f[1551],out[1551],rsc[7]={0};
    size_t n,en,i,j;unsigned long seed=0x12345678;
    assert(!dwa_data_create(group,ap)&&!dwa_data_create(sta,sta));
    d=dwa_data_create(sta,ap);assert(d);ethernet(e,64,0,0);
    assert(dwa_data_encrypt(d,e,64,f,sizeof(f),&n)<0&&n==0);
    assert(dwa_data_install_ccmp(d,1,1,tk,NULL,0)<0);
    assert(dwa_data_install_ccmp(d,0,4,gtk,NULL,0)<0);
    assert(dwa_data_install_ccmp(d,1,0,tk,rsc,7)<0);
    assert(dwa_data_install_ccmp(d,1,0,tk,NULL,1)<0);dwa_data_destroy(d);
    d=ready();memset(out,0x55,sizeof(out));
    for(i=0;i<14;++i)assert(dwa_data_encrypt(d,e,i,out,sizeof(out),&en)<0&&en==0);
    assert(dwa_data_encrypt(d,e,sizeof(e),out,sizeof(out),&en)<0&&en==0);
    assert(dwa_data_encrypt(d,e,64,out,97,&en)<0&&en==0);
    e[6]^=2;assert(dwa_data_encrypt(d,e,64,out,sizeof(out),&en)<0);e[6]^=2;
    e[12]=0x88;e[13]=0x8e;assert(dwa_data_encrypt(d,e,64,out,sizeof(out),&en)<0);
    for(i=0;i<sizeof(out);++i)assert(out[i]==0x55);
    ethernet(e,64,1,0);n=incoming(f,e,64,-1,0,1,tk);
    assert(dwa_data_decrypt(d,f,n,out,63,&en)<0&&en==0);
    assert(!dwa_data_decrypt(d,f,n,out,64,&en));
    /* Bounded deterministic malformed traffic, under host ASan/UBSan and PPC. */
    for(i=0;i<10000;++i) {
        seed=(seed*1664525UL+1013904223UL)&0xffffffffUL;n=seed%sizeof(f);
        for(j=0;j<n;++j){seed=(seed*1664525UL+1013904223UL)&0xffffffffUL;f[j]=(unsigned char)(seed>>24);}
        rejected(d,f,n);
    }
    dwa_data_destroy(d);
}
static void encrypted_control(void)
{
    struct dwa_data *d=ready();unsigned char e[64],f[128],out[64];size_t n,en;
    ethernet(e,64,1,0);e[12]=0x88;e[13]=0x8e;
    n=incoming(f,e,64,-1,0,1,tk);
    assert(dwa_data_decrypt(d,f,n,out,sizeof(out),&en)<0);
    assert(!dwa_data_decrypt_network(d,f,n,out,sizeof(out),&en));
    assert(en==64&&!memcmp(e,out,64));
    assert(dwa_data_decrypt_network(d,f,n,out,sizeof(out),&en)<0);
    dwa_data_destroy(d);
}
int main(void)
{
    encrypted_control();rfc_vector();lengths();mutations();counters_and_keys();bounds_and_random();
    puts("PASS: RFC3610 vector and independent OpenSSL CCM oracle; all 1501 Ethernet lengths TX/RX, QoS/PTK/GTK, unaligned exact buffers");
    puts("PASS: MIC/header/ciphertext mutations, all truncations, output isolation, replay/RSC per key and TID (non-QoS shares TID 0), reinstall/replacement rejection, terminal key clear");
    puts("PASS: 48-bit PN overflow/carry, 4100 abandoned TX reservations and sequence wrap, closed port, strict framing, 10000 malformed frames");
    return 0;
}
