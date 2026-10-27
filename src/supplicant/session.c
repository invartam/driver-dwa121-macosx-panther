/* MIT-licensed station/supplicant coordinator. No radio I/O is simulated in
 * this library: the caller must provide a real, checked transport backend. */
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "session.h"
#include "../net80211/eapol.h"
#include "../control/frame_queue.h"
struct dwa_session {
    struct rtl_station station;
    struct dwa_wpa2 *wpa;
    struct dwa_session_ops ops;
    int stopped;
};
static void management_timeout(void *,void *);
static void stop_transport(struct dwa_session *s)
{
    eloop_cancel_timeout(management_timeout,s,NULL);
    if(!s->stopped){s->stopped=1;s->ops.stop(s->ops.security.cookie);}
    s->station.state=RTL_STA_FAILED;s->station.tx_length=0;
}
static void failure(void *cookie,unsigned int reason)
{
    struct dwa_session *s=cookie;
    stop_transport(s);s->ops.security.failed(s->ops.security.cookie,reason);
}
static int fail_outside_wpa(struct dwa_session *s,unsigned int reason)
{
    dwa_wpa2_disconnect(s->wpa);failure(s,reason);return -1;
}
static int send_eapol(void *cookie,const unsigned char *f,size_t n)
{
    struct dwa_session *s=cookie;
    if(s->stopped||(s->station.state!=RTL_STA_KEYS&&s->station.state!=RTL_STA_AUTHORIZED))return -1;
    return s->ops.security.send_eapol(s->ops.security.cookie,f,n);
}
static int install(void *cookie,int pair,unsigned int index,const unsigned char *k,
    const unsigned char *rsc,size_t rsc_n)
{
    struct dwa_session *s=cookie;
    if(s->stopped||(s->station.state!=RTL_STA_KEYS&&s->station.state!=RTL_STA_AUTHORIZED))return -1;
    return s->ops.security.install_ccmp(s->ops.security.cookie,pair,index,k,rsc,rsc_n);
}
static int port(void *cookie,int enabled)
{
    struct dwa_session *s=cookie;int error;
    if(enabled&&(s->stopped||(s->station.state!=RTL_STA_KEYS&&s->station.state!=RTL_STA_AUTHORIZED)))return -1;
    error=s->ops.security.port(s->ops.security.cookie,enabled);
    if(!error&&enabled&&s->station.state==RTL_STA_KEYS)
        return rtl_station_keys_ready(&s->station,1);
    return error;
}
static void clear(void *cookie)
{
    struct dwa_session *s=cookie;s->ops.security.clear_keys(s->ops.security.cookie);
}
static int send_management(struct dwa_session *s)
{
    eloop_cancel_timeout(management_timeout,s,NULL);
    if(s->ops.send_management(s->ops.security.cookie,s->station.tx,s->station.tx_length)||
       eloop_register_timeout(1,0,management_timeout,s,NULL))return fail_outside_wpa(s,16);
    return 1;
}
static void management_timeout(void *cookie,void *unused)
{
    struct dwa_session *s=cookie;(void)unused;
    if(s->stopped)return;
    if(rtl_station_timeout(&s->station)==1)send_management(s);
    else fail_outside_wpa(s,16);
}
struct dwa_session *dwa_session_create(const struct dwa_session_ops *ops,
    const unsigned char mac[6],const unsigned char *beacon,size_t n,
    const unsigned char *ssid,size_t ssid_n,unsigned int channel,
    unsigned int age,const unsigned char pmk[32])
{
    struct dwa_session *s;struct dwa_wpa2_ops security;
    if(!ops||!ops->prepare||!ops->send_management||!ops->associated||!ops->stop||
       !ops->security.port||!ops->security.clear_keys||!ops->security.failed||
       !ops->security.send_eapol||!ops->security.install_ccmp||ops->security.handshake_complete||!pmk||n>4092||ssid_n>32)return NULL;
    s=os_zalloc(sizeof(*s));if(!s)return NULL;s->ops=*ops;
    if(rtl_station_begin(&s->station,mac,beacon,(unsigned int)n,ssid,
       (unsigned int)ssid_n,channel,age)!=1){os_free(s);return NULL;}
    memset(&security,0,sizeof(security));
    security.cookie=s;security.send_eapol=send_eapol;security.install_ccmp=install;
    security.port=port;security.clear_keys=clear;security.failed=failure;
    s->wpa=dwa_wpa2_create(&security,mac,s->station.target.bssid,ssid,ssid_n,pmk,
        s->station.target.rsn_ie,s->station.target.rsn_length);
    if(!s->wpa){os_free(s);return NULL;}
    if(ops->prepare(ops->security.cookie,&s->station)||send_management(s)<0) {
        dwa_session_destroy(s);return NULL;
    }
    return s;
}
int dwa_session_receive_management(struct dwa_session *s,const unsigned char *f,size_t n)
{
    unsigned int before;int result;
    if(!s||s->stopped||n>4092)return -1;
    before=s->station.state;result=rtl_station_receive(&s->station,f,(unsigned int)n);
    if(s->station.state==RTL_STA_FAILED)return fail_outside_wpa(s,s->station.status);
    if(result==1)return send_management(s);
    if(before==RTL_STA_ASSOC&&s->station.state==RTL_STA_KEYS) {
        eloop_cancel_timeout(management_timeout,s,NULL);
        if(s->ops.associated(s->ops.security.cookie,&s->station)||dwa_wpa2_associated(s->wpa))
            return fail_outside_wpa(s,16);
    }
    return result;
}
int dwa_session_receive_eapol(struct dwa_session *s,const unsigned char src[6],
    const unsigned char *f,size_t n)
{
    if(!s||s->stopped||(s->station.state!=RTL_STA_KEYS&&s->station.state!=RTL_STA_AUTHORIZED))return -1;
    return dwa_wpa2_receive(s->wpa,src,f,n);
}
unsigned int dwa_session_state(const struct dwa_session *s)
{ return s?s->station.state:RTL_STA_IDLE; }
int dwa_session_receive_80211(struct dwa_session *s,const unsigned char *f,size_t n,
    unsigned int age_ms)
{
    struct rtl_eapol_view v;
    if(!s||s->stopped||!f||n<24||n>DWA_FRAME_MAX||age_ms>DWA_FRAME_MAX_AGE_MS)return -1;
    if(f[0]==0xb0||f[0]==0x10||f[0]==0xa0||f[0]==0xc0)
        return dwa_session_receive_management(s,f,n);
    if(rtl_eapol_from_80211(f,(unsigned int)n,s->station.mac,s->station.target.bssid,&v))return -1;
    return dwa_session_receive_eapol(s,f+10,v.data,v.length);
}
int dwa_session_authorized(const struct dwa_session *s)
{ return s&&!s->stopped&&s->station.state==RTL_STA_AUTHORIZED&&dwa_wpa2_authorized(s->wpa); }
void dwa_session_destroy(struct dwa_session *s)
{ if(s){dwa_wpa2_destroy(s->wpa);stop_transport(s);bin_clear_free(s,sizeof(*s));} }
