/* Synthetic AP oracle uses OpenSSL; the supplicant uses hostap's independent
 * internal crypto. No radio, real network credential, or installed device key. */
#include "../src/supplicant/wpa2.h"
#include "../src/supplicant/session.h"
#include "../src/supplicant/data.h"
#include "../src/net80211/station.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
static const unsigned char sta[6]={2,0,0,0,0,1},ap[6]={2,0,0,0,0,2};
struct test {
    struct dwa_data *data;
    unsigned char output[2048],ptk[48],pair_key[16],group_key[16];
    size_t output_n;
    unsigned int sent,pair_installs,group_installs,cleared,failures,opened;
    int port,fail_install,fail_send,fail_port,verified,fail_verified;
    unsigned char management[512];size_t management_n;
    unsigned int prepared,mgmt_sent,associated,stopped;
    int fail_prepare,fail_association;
};
static void put16(unsigned char *p,unsigned int v){p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static int send_frame(void *c,const unsigned char *b,size_t n)
{
    struct test *t=c;if(t->fail_send)return -1;
    assert(n<=sizeof(t->output));memcpy(t->output,b,n);t->output_n=n;t->sent++;return 0;
}
static int install(void *c,int pair,unsigned int index,const unsigned char *key,const unsigned char *rsc,size_t rsc_n)
{
    struct test *t=c;(void)rsc;assert(rsc_n<=6);
    if(t->fail_install)return -1;
    if(t->data&&dwa_data_install_ccmp(t->data,pair,index,key,rsc,rsc_n))return -1;
    if(pair){assert(index==0);t->pair_installs++;memcpy(t->pair_key,key,16);}
    else {assert(index==1||index==2);t->group_installs++;memcpy(t->group_key,key,16);}
    return 0;
}
static int port(void *c,int enabled){struct test *t=c;if(enabled&&t->fail_port)return -1;if(t->data&&dwa_data_port(t->data,enabled))return -1;t->port=enabled;if(enabled)t->opened++;return 0;}
static void clear(void *c){struct test *t=c;t->cleared++;if(t->data)dwa_data_clear(t->data);memset(t->pair_key,0,16);memset(t->group_key,0,16);}
static void failed(void *c,unsigned int why){struct test *t=c;assert(why);t->failures++;}
static int verified(void *c){struct test *t=c;assert(!t->port&&t->pair_installs&&t->group_installs);t->verified++;return t->fail_verified?-1:0;}
static int prepare(void *c,const struct rtl_station *s)
{struct test *t=c;assert(!t->port&&s->target.channel==11);t->prepared++;return t->fail_prepare?-1:0;}
static int management(void *c,const unsigned char *f,size_t n)
{struct test *t=c;assert(t->prepared&&!t->port&&n<=512);memcpy(t->management,f,n);t->management_n=n;t->mgmt_sent++;return t->fail_send?-1:0;}
static int associated(void *c,const struct rtl_station *s)
{struct test *t=c;assert(!t->port&&s->aid==1);t->associated++;return t->fail_association?-1:0;}
static void stop(void *c){struct test *t=c;t->stopped++;t->port=0;}
static struct dwa_session *session(struct test *t,const unsigned char pmk[32])
{
    struct dwa_session_ops ops;unsigned char b[128];size_t n=36;
    memset(&ops,0,sizeof(ops));ops.security.cookie=t;ops.security.send_eapol=send_frame;
    ops.security.install_ccmp=install;ops.security.port=port;ops.security.clear_keys=clear;ops.security.failed=failed;
    ops.prepare=prepare;ops.send_management=management;ops.associated=associated;ops.stop=stop;
    memset(b,0,sizeof(b));b[0]=0x80;memset(b+4,255,6);memcpy(b+10,ap,6);memcpy(b+16,ap,6);
    b[32]=100;b[34]=0x11;b[n++]=0;b[n++]=4;memcpy(b+n,"IEEE",4);n+=4;
    b[n++]=3;b[n++]=1;b[n++]=11;b[n++]=1;b[n++]=4;
    b[n++]=0x82;b[n++]=0x84;b[n++]=0x8b;b[n++]=0x96;memcpy(b+n,rtl_station_rsn,22);n+=22;
    return dwa_session_create(&ops,sta,b,n,(const unsigned char *)"IEEE",4,11,0,pmk);
}
static void mgmt_reply(unsigned char *f,unsigned char subtype)
{memset(f,0,2048);f[0]=subtype;memcpy(f+4,sta,6);memcpy(f+10,ap,6);memcpy(f+16,ap,6);}
static size_t wifi(unsigned char *out,const unsigned char *e,size_t n,int qos)
{
    static const unsigned char llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    size_t h=qos?26:24;
    assert(n<=2048);memset(out,0,h+8);out[0]=qos?0x88:8;out[1]=2;
    memcpy(out+4,sta,6);memcpy(out+10,ap,6);memcpy(out+16,ap,6);
    memcpy(out+h,llc,8);memcpy(out+h+8,e,n);return h+8+n;
}
static void associate_session(struct dwa_session *s,struct test *t)
{
    unsigned char f[2048];
    assert(s&&dwa_session_state(s)==RTL_STA_AUTH&&t->mgmt_sent==1&&t->management[0]==0xb0);
    mgmt_reply(f,0xb0);f[26]=2;
    assert(dwa_session_receive_80211(s,f,30,5001)<0&&t->mgmt_sent==1);
    assert(dwa_session_receive_80211(s,f,30,0)==1&&t->mgmt_sent==2&&t->management[0]==0);
    assert(!memcmp(t->management+t->management_n-22,rtl_station_rsn,22));
    mgmt_reply(f,0x10);f[24]=0x11;f[28]=1;f[29]=0xc0;
    f[30]=1;f[31]=4;f[32]=0x82;f[33]=0x84;f[34]=0x8b;f[35]=0x96;
    if(t->fail_association)assert(dwa_session_receive_management(s,f,36)<0);
    else assert(dwa_session_receive_80211(s,f,36,0)==0&&dwa_session_state(s)==RTL_STA_KEYS);
    assert(!t->port&&t->associated==1);
}
static struct dwa_wpa2 *create_mode(struct test *t,const unsigned char pmk[32],int probe)
{
    struct dwa_wpa2_ops ops;struct dwa_wpa2 *s;
    memset(t,0,sizeof(*t));memset(&ops,0,sizeof(ops));ops.cookie=t;ops.send_eapol=send_frame;
    ops.install_ccmp=install;ops.port=port;ops.clear_keys=clear;ops.failed=failed;
    if(probe)ops.handshake_complete=verified;
    s=dwa_wpa2_create(&ops,sta,ap,(const unsigned char *)"IEEE",4,pmk,rtl_station_rsn,22);
    assert(s&&!dwa_wpa2_authorized(s)&&!t->port);
    assert(dwa_wpa2_associated(s)==0);assert(dwa_wpa2_associated(s)<0);return s;
}
static struct dwa_wpa2 *create(struct test *t,const unsigned char pmk[32])
{return create_mode(t,pmk,0);}
static void prf(const unsigned char pmk[32],const unsigned char snonce[32],unsigned char ptk[48])
{
    const char label[]="Pairwise key expansion";
    unsigned char in[128],anonce[32],digest[20];unsigned int i,pos,len,total=0;
    memset(anonce,0x11,32);memcpy(in,label,sizeof(label));pos=sizeof(label);
    memcpy(in+pos,sta,6);pos+=6;memcpy(in+pos,ap,6);pos+=6;
    if(memcmp(anonce,snonce,32)<0){memcpy(in+pos,anonce,32);memcpy(in+pos+32,snonce,32);}
    else {memcpy(in+pos,snonce,32);memcpy(in+pos+32,anonce,32);}pos+=64;
    for(i=0;total<48;i++) {
        unsigned int take=48-total;if(take>20)take=20;in[pos]=(unsigned char)i;
        assert(HMAC(EVP_sha1(),pmk,32,in,pos+1,digest,&len)&&len==20);
        memcpy(ptk+total,digest,take);total+=take;
    }
}
static void mic(unsigned char *frame,size_t n,const unsigned char *kck)
{
    unsigned char out[20];unsigned int len;
    memset(frame+81,0,16);assert(HMAC(EVP_sha1(),kck,16,frame,n,out,&len)&&len==20);
    memcpy(frame+81,out,16);
}
static void verify_mic(const struct test *t)
{
    unsigned char f[2048];memcpy(f,t->output,t->output_n);mic(f,t->output_n,t->ptk);
    assert(!memcmp(f+81,t->output+81,16));
}
static void wrap(const unsigned char *kek,const unsigned char *plain,unsigned int n,unsigned char *out)
{
    AES_KEY aes;unsigned char b[16];unsigned int i,j,k,x;
    assert(n>=16&&n%8==0&&AES_set_encrypt_key(kek,128,&aes)==0);
    memset(out,0xa6,8);memcpy(out+8,plain,n);
    for(j=0;j<6;j++)for(i=1;i<=n/8;i++) {
        memcpy(b,out,8);memcpy(b+8,out+8*i,8);AES_encrypt(b,b,&aes);
        x=(n/8)*j+i;for(k=0;k<8;k++){b[7-k]^=(unsigned char)x;x>>=8;}
        memcpy(out,b,8);memcpy(out+8*i,b+8,8);
    }
}
static size_t message(unsigned char *f,unsigned int flags,unsigned int counter)
{
    memset(f,0,2048);f[0]=2;f[1]=3;put16(f+2,95);f[4]=2;put16(f+5,flags);put16(f+7,16);
    f[16]=(unsigned char)counter;memset(f+17,0x11,32);return 99;
}
static size_t m3(unsigned char *f,const unsigned char ptk[48],unsigned int replay,int group,unsigned int keyid)
{
    unsigned char plain[80];unsigned int n=0,wrapped;
    message(f,group?0x1382:0x13ca,replay);
    if(!group){memcpy(plain,rtl_station_rsn,22);n=22;}else memset(f+17,0,32);
    plain[n++]=0xdd;plain[n++]=22;plain[n++]=0;plain[n++]=15;plain[n++]=172;plain[n++]=1;
    plain[n++]=(unsigned char)keyid;plain[n++]=0;memset(plain+n,0x30+keyid,16);n+=16;
    if(n%8){plain[n++]=0xdd;while(n%8)plain[n++]=0;}
    wrapped=n+8;wrap(ptk+16,plain,n,f+99);put16(f+97,wrapped);put16(f+2,95+wrapped);
    mic(f,99+wrapped,ptk);return 99+wrapped;
}
static void m1_m2(struct dwa_wpa2 *s,struct test *t,const unsigned char pmk[32])
{
    unsigned char f[2048];size_t n=message(f,0x8a,1);
    assert(dwa_wpa2_receive(s,ap,f,n)>=0&&t->sent==1&&t->output_n==121);
    assert(t->output[5]==1&&t->output[6]==0x0a);
    assert(!memcmp(t->output+99,rtl_station_rsn,22));
    prf(pmk,t->output+17,t->ptk);verify_mic(t);
    assert(!t->pair_installs&&!t->group_installs&&!t->port);
}
int main(void)
{
    struct dwa_wpa2 *s;struct test t;unsigned char pmk[32],reference[32],f[2048],bad[2048],zero[16]={0};
    unsigned int before,i;size_t n;unsigned char salt[]="IEEE";struct dwa_session *connection;
    u32 endian_word=host_to_be32(0x12345678);const unsigned char endian_expected[4]={0x12,0x34,0x56,0x78};
    assert(sizeof(u32)==4&&!memcmp(&endian_word,endian_expected,4));
    assert(eloop_init()==0);
    assert(dwa_wpa2_pmk("password",(const unsigned char *)"IEEE",4,pmk)==0);
    assert(PKCS5_PBKDF2_HMAC_SHA1("password",8,salt,4,4096,32,reference)==1);
    assert(!memcmp(pmk,reference,32));
    memset(bad,0x55,32);assert(dwa_wpa2_pmk("short",(const unsigned char *)"IEEE",4,bad)<0);
    assert(!memcmp(bad,zero,16)&&!memcmp(bad+16,zero,16));
    /* Real WPA callbacks control the software data boundary. Closing after
     * deauthentication must erase keys and prohibit reopening the same TK. */
    {
        unsigned char eth[60]={0},encrypted[128];size_t encrypted_n;
        memset(&t,0,sizeof(t));t.data=dwa_data_create(sta,ap);assert(t.data);
        connection=session(&t,pmk);assert(connection);
        memcpy(eth,ap,6);memcpy(eth+6,sta,6);eth[12]=8;
        assert(dwa_data_encrypt(t.data,eth,60,encrypted,128,&encrypted_n)<0);
        associate_session(connection,&t);n=message(f,0x8a,1);
        assert(dwa_session_receive_eapol(connection,ap,f,n)>=0);
        prf(pmk,t.output+17,t.ptk);n=m3(f,t.ptk,2,0,1);
        memcpy(bad,f,n);bad[81]^=1;dwa_session_receive_eapol(connection,ap,bad,n);
        assert(!dwa_data_authorized(t.data));
        assert(dwa_session_receive_eapol(connection,ap,f,n)>=0&&dwa_data_authorized(t.data));
        assert(!dwa_data_encrypt(t.data,eth,60,encrypted,128,&encrypted_n)&&encrypted[24]==1);
        n=m3(f,t.ptk,3,0,1);dwa_session_receive_eapol(connection,ap,f,n);
        assert(!dwa_data_encrypt(t.data,eth,60,encrypted,128,&encrypted_n)&&encrypted[24]==2);
        mgmt_reply(f,0xc0);f[24]=3;dwa_session_receive_management(connection,f,26);
        assert(!dwa_data_authorized(t.data)&&dwa_data_port(t.data,1)<0);
        assert(dwa_data_encrypt(t.data,eth,60,encrypted,128,&encrypted_n)<0);
        dwa_session_destroy(connection);dwa_data_destroy(t.data);t.data=NULL;
    }
    s=create(&t,pmk);m1_m2(s,&t,pmk);n=m3(f,t.ptk,2,0,1);
    memcpy(bad,f,n);bad[81]^=1;dwa_wpa2_receive(s,ap,bad,n);
    assert(!t.port&&!t.pair_installs&&!t.group_installs);
    assert(dwa_wpa2_receive(s,sta,f,n)<0&&!t.port); /* wrong AP source */
    for(i=0;i<n;i++)dwa_wpa2_receive(s,ap,f,i);
    assert(!t.port&&!t.pair_installs);
    assert(dwa_wpa2_receive(s,ap,f,n)>=0&&dwa_wpa2_authorized(s)&&t.port);
    assert(t.pair_installs==1&&t.group_installs==1&&!memcmp(t.pair_key,t.ptk+32,16));
    for(i=0;i<16;i++)assert(t.group_key[i]==0x31);
    verify_mic(&t);assert(t.output_n==99&&t.output[5]==3&&t.output[6]==0x0a);
    before=t.sent;dwa_wpa2_receive(s,ap,f,n); /* exact replay cannot reinstall */
    assert(t.pair_installs==1&&t.group_installs==1&&t.sent==before);
    n=m3(f,t.ptk,3,0,1);dwa_wpa2_receive(s,ap,f,n); /* retransmitted M3, higher replay */
    assert(t.pair_installs==1&&t.group_installs==1&&t.port);verify_mic(&t);
    n=m3(f,t.ptk,4,1,2);assert(dwa_wpa2_receive(s,ap,f,n)>=0&&t.port);
    assert(t.pair_installs==1&&t.group_installs==2);verify_mic(&t);
    n=m3(f,t.ptk,5,1,2);dwa_wpa2_receive(s,ap,f,n);
    assert(t.group_installs==2&&t.port); /* duplicate GTK never resets PN */
    dwa_wpa2_disconnect(s);assert(!t.port&&!dwa_wpa2_authorized(s)&&!memcmp(t.pair_key,zero,16));
    before=t.sent;assert(dwa_wpa2_receive(s,ap,f,n)<0&&t.sent==before);dwa_wpa2_destroy(s);
    s=create(&t,pmk);m1_m2(s,&t,pmk);t.fail_install=1;n=m3(f,t.ptk,2,0,1);
    dwa_wpa2_receive(s,ap,f,n);assert(!t.port&&t.failures&&!dwa_wpa2_authorized(s));dwa_wpa2_destroy(s);
    s=create(&t,pmk);t.fail_send=1;n=message(f,0x8a,1);dwa_wpa2_receive(s,ap,f,n);
    assert(!t.port&&t.failures);dwa_wpa2_destroy(s);
    s=create(&t,pmk);m1_m2(s,&t,pmk);n=m3(f,t.ptk,2,0,1);
    f[100]^=1;mic(f,n,t.ptk);dwa_wpa2_receive(s,ap,f,n);
    assert(!t.port&&!t.pair_installs&&!t.group_installs); /* authenticated bad AES wrap */
    dwa_wpa2_destroy(s);
    s=create(&t,pmk);m1_m2(s,&t,pmk);t.fail_port=1;n=m3(f,t.ptk,2,0,1);
    dwa_wpa2_receive(s,ap,f,n);assert(!t.port&&t.failures&&!dwa_wpa2_authorized(s));dwa_wpa2_destroy(s);
    s=create_mode(&t,pmk,1);m1_m2(s,&t,pmk);n=m3(f,t.ptk,2,0,1);
    assert(dwa_wpa2_receive(s,ap,f,n)>=0&&dwa_wpa2_handshake_verified(s));
    assert(t.verified==1&&!t.port&&!t.opened&&!dwa_wpa2_authorized(s));verify_mic(&t);
    dwa_wpa2_disconnect(s);assert(!dwa_wpa2_handshake_verified(s));dwa_wpa2_destroy(s);
    s=create_mode(&t,pmk,1);m1_m2(s,&t,pmk);t.fail_verified=1;n=m3(f,t.ptk,2,0,1);
    dwa_wpa2_receive(s,ap,f,n);assert(t.failures&&!t.port&&!dwa_wpa2_handshake_verified(s));dwa_wpa2_destroy(s);
    /* Actual coordinator: auth -> assoc -> key exchange -> controlled port. */
    memset(&t,0,sizeof(t));connection=session(&t,pmk);assert(connection);
    n=message(f,0x8a,1);assert(dwa_session_receive_eapol(connection,ap,f,n)<0&&t.sent==0);
    associate_session(connection,&t);
    n=message(f,0x8a,1);assert(dwa_session_receive_eapol(connection,ap,f,n)>=0&&t.sent==1);
    prf(pmk,t.output+17,t.ptk);verify_mic(&t);n=m3(f,t.ptk,2,0,1);
    assert(dwa_session_receive_eapol(connection,ap,f,n)>=0&&dwa_session_authorized(connection));
    assert(dwa_session_state(connection)==RTL_STA_AUTHORIZED&&t.port&&t.pair_installs==1);
    mgmt_reply(f,0xc0);f[24]=3;assert(dwa_session_receive_management(connection,f,26)<0);
    assert(!t.port&&!dwa_session_authorized(connection)&&t.stopped==1);
    dwa_session_destroy(connection);assert(t.stopped==1);
    /* The actual queue boundary: borrowed full 802.11 frames, both header
     * layouts, stale/wrong-peer rejection, and no reinstall on repeated M3. */
    for(i=0;i<2;++i) {
        unsigned char raw[2082];size_t raw_n;
        memset(&t,0,sizeof(t));connection=session(&t,pmk);assert(connection);
        n=message(f,0x8a,1);raw_n=wifi(raw,f,n,(int)i);
        assert(dwa_session_receive_80211(connection,raw,raw_n,0)<0&&!t.sent);
        associate_session(connection,&t);
        assert(dwa_session_receive_80211(connection,raw,raw_n,5001)<0&&!t.sent);
        raw[10]^=2;assert(dwa_session_receive_80211(connection,raw,raw_n,0)<0&&!t.sent);raw[10]^=2;
        raw[1]|=0x40;assert(dwa_session_receive_80211(connection,raw,raw_n,0)<0&&!t.sent);raw[1]=2;
        assert(dwa_session_receive_80211(connection,raw,raw_n,0)>=0&&t.sent==1);
        prf(pmk,t.output+17,t.ptk);verify_mic(&t);n=m3(f,t.ptk,2,0,1);
        raw_n=wifi(raw,f,n,(int)i);
        assert(dwa_session_receive_80211(connection,raw,raw_n,0)>=0&&dwa_session_authorized(connection));
        assert(t.pair_installs==1&&t.group_installs==1);verify_mic(&t);
        before=t.sent;dwa_session_receive_80211(connection,raw,raw_n,0);
        assert(t.pair_installs==1&&t.group_installs==1&&t.sent==before&&t.port);
        n=m3(f,t.ptk,3,0,1);raw_n=wifi(raw,f,n,(int)i);
        assert(dwa_session_receive_80211(connection,raw,raw_n,0)>=0);
        assert(t.pair_installs==1&&t.group_installs==1&&t.port);verify_mic(&t);
        mgmt_reply(f,0xc0);f[24]=3;
        assert(dwa_session_receive_80211(connection,f,26,0)<0&&!t.port&&t.stopped==1);
        dwa_session_destroy(connection);assert(t.stopped==1);
    }
    memset(&t,0,sizeof(t));t.fail_prepare=1;assert(!session(&t,pmk)&&t.stopped==1&&!t.mgmt_sent);
    memset(&t,0,sizeof(t));t.fail_association=1;connection=session(&t,pmk);assert(connection);
    associate_session(connection,&t);assert(t.stopped==1&&!t.sent&&!t.port);dwa_session_destroy(connection);
    memset(&t,0,sizeof(t));connection=session(&t,pmk);assert(connection);eloop_run();
    assert(t.mgmt_sent==3&&t.stopped==1&&!t.port&&t.failures==1);dwa_session_destroy(connection);
    /* Wrong PSK: AP derives PTK with the expected PMK; STA cannot accept M3. */
    memcpy(reference,pmk,32);reference[0]^=1;s=create(&t,reference);
    n=message(f,0x8a,1);assert(dwa_wpa2_receive(s,ap,f,n)>=0&&t.sent==1);
    prf(pmk,t.output+17,t.ptk);n=m3(f,t.ptk,2,0,1);dwa_wpa2_receive(s,ap,f,n);
    assert(!t.port&&!t.pair_installs&&!t.group_installs);eloop_run();
    assert(t.failures==1&&!dwa_wpa2_authorized(s));dwa_wpa2_destroy(s);
    eloop_destroy();
    puts("PASS: hostap WPA2 core vs OpenSSL AP oracle: PMK, M1-M4/MIC/PTK/GTK, bad MIC, truncations, replay, M3/GTK reinstall protection, group rekey, failed key/TX, closed port and teardown");
    puts("PASS: integrated auth/association/WPA2 session, early EAPOL rejection, prepare/association failure, deauthentication, bounded timed retries and cleanup");
    puts("PASS: 802.11 queue bridge, QoS/non-QoS M1-M4, MIC oracle, peer/age/protection rejection and retransmission without key reinstall");
    puts("PASS: handshake diagnostic verifies M3/M4 and installed keys without authorizing a data port; completion failure closes and clears");
    puts("PASS: WPA session callbacks install software CCMP keys, gate Ethernet until M3/M4, retain TX PN after repeated M3 and close permanently on deauthentication");
    return 0;
}
