/* MIT-licensed adapter to hostap 2.12, with a Panther endian header patch. */
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "crypto/sha1.h"
#include "rsn_supp/wpa.h"
#include "wpa2.h"
#include "../net80211/station.h"
struct dwa_wpa2 {
    struct wpa_sm *sm;
    struct dwa_wpa2_ops ops;
    enum wpa_states state;
    unsigned char mac[6],bssid[6],ssid[32],ap_rsn[257];
    size_t ssid_len,ap_rsn_len;
    int active,authorized,pairwise,group,failed,verified;
};
static void deadline(void *,void *);
static int close_port(struct dwa_wpa2 *s)
{
    int error;s->authorized=0;s->verified=0;error=s->ops.port(s->ops.cookie,0);
    s->ops.clear_keys(s->ops.cookie);s->pairwise=s->group=0;
    return error;
}
static void failure(void *cookie,u16 reason)
{
    struct dwa_wpa2 *s=cookie;
    if(s->failed)return;
    eloop_cancel_timeout(deadline,s,NULL);
    s->active=0;s->failed=1;s->state=WPA_DISCONNECTED;
    close_port(s);s->ops.failed(s->ops.cookie,reason);
}
static void deadline(void *cookie,void *unused)
{ (void)unused;failure(cookie,15); }
static void cancel_timeout(void *cookie)
{ eloop_cancel_timeout(deadline,cookie,NULL); }
static void set_state(void *cookie,enum wpa_states state)
{
    struct dwa_wpa2 *s=cookie;
    s->state=state;
    if(state==WPA_COMPLETED) {
        if(!s->active||s->failed||!s->pairwise||!s->group){failure(s,15);return;}
        if(s->ops.handshake_complete) {
            if(s->ops.handshake_complete(s->ops.cookie)){failure(s,15);return;}
        } else {
            if(s->ops.port(s->ops.cookie,1)){failure(s,15);return;}
            s->authorized=1;
        }
        s->verified=1;cancel_timeout(s);
    }
}
static enum wpa_states get_state(void *cookie)
{ return ((struct dwa_wpa2 *)cookie)->state; }
static void reconnect(void *cookie) { failure(cookie,15); }
static void *network(void *cookie) { return cookie; }
static int bssid(void *cookie,u8 *out)
{ memcpy(out,((struct dwa_wpa2 *)cookie)->bssid,6);return 0; }
static int send_eapol(void *cookie,const u8 *dest,u16 proto,const u8 *data,size_t n)
{
    struct dwa_wpa2 *s=cookie;
    if(!s->active||s->failed||proto!=0x888e||memcmp(dest,s->bssid,6)||n>2048)return -1;
    if(s->ops.send_eapol(s->ops.cookie,data,n)){failure(s,15);return -1;}
    return 0;
}
static int set_key(void *cookie,int link,enum wpa_alg alg,const u8 *addr,
    int index,int tx,const u8 *seq,size_t seq_n,const u8 *key,size_t key_n,
    enum key_flag flag)
{
    struct dwa_wpa2 *s=cookie;
    static const u8 broadcast[6]={255,255,255,255,255,255};
    int pairwise=addr&&!memcmp(addr,s->bssid,6);
    (void)tx;
    /* 2.12 optionally stages a NEXT/RX-only key before M4. This backend
     * contract has no staging/activation operation: decline that optional
     * optimization, never install the same key twice or reset its PN. */
    if(flag!=(pairwise?KEY_FLAG_PAIRWISE_RX_TX:KEY_FLAG_GROUP_RX))return -1;
    if(!s->active||s->failed||link!=-1||alg!=WPA_ALG_CCMP||key_n!=16||
       !key||index<0||index>3||seq_n>6||(seq_n&&!seq)||
       (pairwise?index!=0:(!addr||memcmp(addr,broadcast,6))))return -1;
    if(s->ops.install_ccmp(s->ops.cookie,pairwise,(unsigned int)index,key,seq,seq_n))return -1;
    if(pairwise)s->pairwise=1;else s->group=1;
    return 0;
}
static int beacon(void *cookie)
{
    struct dwa_wpa2 *s=cookie;
    return wpa_sm_set_ap_rsn_ie(s->sm,s->ap_rsn,s->ap_rsn_len);
}
static u8 *alloc_eapol(void *cookie,u8 type,const void *data,u16 data_len,
    size_t *msg_len,void **data_pos)
{
    u8 *out;(void)cookie;
    *msg_len=4+data_len;out=os_zalloc(*msg_len);if(!out)return NULL;
    out[0]=2;out[1]=type;WPA_PUT_BE16(out+2,data_len);
    if(data)memcpy(out+4,data,data_len);*data_pos=out+4;return out;
}
static int protection(void *cookie,const u8 *addr,int type,int key_type)
{ (void)cookie;(void)addr;(void)type;(void)key_type;return 0; }
static int add_pmkid(void *c,void *n,const u8 *b,const u8 *id,const u8 *f,
    const u8 *p,size_t pn,u32 life,u8 threshold,int akmp)
{ (void)c;(void)n;(void)b;(void)id;(void)f;(void)p;(void)pn;(void)life;(void)threshold;(void)akmp;return -1; }
static int remove_pmkid(void *c,void *n,const u8 *b,const u8 *id,const u8 *f)
{ (void)c;(void)n;(void)b;(void)id;(void)f;return 0; }
static int unicast(const u8 *m)
{ unsigned int i,v=0;if(!m||m[0]&1)return 0;for(i=0;i<6;i++)v|=m[i];return v!=0; }
struct dwa_wpa2 *dwa_wpa2_create(const struct dwa_wpa2_ops *ops,
    const u8 mac[6],const u8 ap[6],const u8 *ssid,size_t ssid_n,const u8 pmk[32],
    const u8 *rsn,size_t rsn_n)
{
    struct dwa_wpa2 *s;struct wpa_sm_ctx *ctx;struct rsn_supp_config config;
    struct rtl_rsn policy;
    if(!ops||!ops->send_eapol||!ops->install_ccmp||!ops->port||!ops->clear_keys||!ops->failed||
       !unicast(mac)||!unicast(ap)||!memcmp(mac,ap,6)||!ssid||!ssid_n||ssid_n>32||!pmk||
       !rsn||rsn_n>257||rtl_rsn_parse(rsn,(unsigned int)rsn_n,&policy)||
       !policy.psk||!policy.group_ccmp||!policy.pairwise_ccmp||(policy.capabilities&0x40))return NULL;
    s=os_zalloc(sizeof(*s));ctx=os_zalloc(sizeof(*ctx));
    if(!s||!ctx){os_free(s);os_free(ctx);return NULL;}
    s->ops=*ops;memcpy(s->mac,mac,6);memcpy(s->bssid,ap,6);
    memcpy(s->ssid,ssid,ssid_n);s->ssid_len=ssid_n;
    memcpy(s->ap_rsn,rsn,rsn_n);s->ap_rsn_len=rsn_n;
    ctx->ctx=s;ctx->msg_ctx=s;ctx->set_state=set_state;ctx->get_state=get_state;
    ctx->deauthenticate=failure;ctx->reconnect=reconnect;ctx->set_key=set_key;
    ctx->get_network_ctx=network;ctx->get_bssid=bssid;ctx->ether_send=send_eapol;
    ctx->get_beacon_ie=beacon;ctx->cancel_auth_timeout=cancel_timeout;
    ctx->alloc_eapol=alloc_eapol;ctx->mlme_setprotection=protection;
    ctx->add_pmkid=add_pmkid;ctx->remove_pmkid=remove_pmkid;
    s->sm=wpa_sm_init(ctx); /* takes ownership of ctx on success */
    if(!s->sm){os_free(ctx);os_free(s);return NULL;}
    memset(&config,0,sizeof(config));config.network_ctx=s;
    config.ssid=s->ssid;config.ssid_len=ssid_n;config.allowed_pairwise_cipher=WPA_CIPHER_CCMP;
    config.wpa_deny_ptk0_rekey=1;
    wpa_sm_set_config(s->sm,&config);wpa_sm_set_own_addr(s->sm,mac);
    wpa_sm_set_param(s->sm,WPA_PARAM_PROTO,WPA_PROTO_RSN);
    wpa_sm_set_param(s->sm,WPA_PARAM_PAIRWISE,WPA_CIPHER_CCMP);
    wpa_sm_set_param(s->sm,WPA_PARAM_GROUP,WPA_CIPHER_CCMP);
    wpa_sm_set_param(s->sm,WPA_PARAM_KEY_MGMT,WPA_KEY_MGMT_PSK);
    wpa_sm_set_param(s->sm,WPA_PARAM_RSN_ENABLED,1);
    wpa_sm_set_param(s->sm,WPA_PARAM_MFP,0);
    wpa_sm_set_param(s->sm,WPA_PARAM_DENY_PTK0_REKEY,1);
    if(wpa_sm_set_ap_rsn_ie(s->sm,rsn,rsn_n)||
       wpa_sm_set_assoc_wpa_ie(s->sm,rtl_station_rsn,RTL_STA_RSN_SIZE)) {
        wpa_sm_deinit(s->sm);os_free(s);return NULL;
    }
    wpa_sm_set_pmk(s->sm,pmk,32,NULL,NULL);
    if(close_port(s)){failure(s,15);wpa_sm_deinit(s->sm);bin_clear_free(s,sizeof(*s));return NULL;}
    return s;
}
int dwa_wpa2_associated(struct dwa_wpa2 *s)
{
    if(!s||s->active||s->failed)return -1;
    if(close_port(s)){failure(s,15);return -1;}
    s->active=1;s->state=WPA_ASSOCIATED;
    wpa_sm_notify_assoc(s->sm,s->bssid);
    if(eloop_register_timeout(10,0,deadline,s,NULL)){failure(s,15);return -1;}
    return 0;
}
int dwa_wpa2_receive(struct dwa_wpa2 *s,const u8 src[6],const u8 *data,size_t n)
{
    if(!s||!s->active||s->failed||!src||memcmp(src,s->bssid,6)||!data||n<99||n>2048||
       data[0]<1||data[0]>2||data[1]!=3||WPA_GET_BE16(data+2)!=(n-4)||data[4]!=2||
       (WPA_GET_BE16(data+5)&7)!=2)return -1;
    return wpa_sm_rx_eapol(s->sm,src,data,n,FRAME_ENCRYPTION_UNKNOWN);
}
void dwa_wpa2_disconnect(struct dwa_wpa2 *s)
{
    if(!s)return;
    s->active=0;s->state=WPA_DISCONNECTED;cancel_timeout(s);close_port(s);
    wpa_sm_notify_disassoc(s->sm);
    /* Fresh object/PMK required on reconnect; no stale nonce/key reuse. */
    s->failed=1;
}
void dwa_wpa2_destroy(struct dwa_wpa2 *s)
{ if(s){dwa_wpa2_disconnect(s);wpa_sm_deinit(s->sm);bin_clear_free(s,sizeof(*s));} }
int dwa_wpa2_authorized(const struct dwa_wpa2 *s)
{ return s&&s->active&&!s->failed&&s->authorized; }
int dwa_wpa2_handshake_verified(const struct dwa_wpa2 *s)
{ return s&&s->active&&!s->failed&&s->verified; }
int dwa_wpa2_pmk(const char *pass,const u8 *ssid,size_t n,u8 pmk[32])
{
    size_t len;int result;
    if(!pmk)return -1;
    forced_memzero(pmk,32);
    if(!pass||!ssid||!n||n>32)return -1;
    for(len=0;len<64&&pass[len];len++){}
    if(len<8||len>63)return -1;
    result=pbkdf2_sha1(pass,ssid,n,4096,pmk,32);
    if(result)forced_memzero(pmk,32);
    return result;
}
