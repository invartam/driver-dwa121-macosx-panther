/* Included only in dwa121-wpa2, sharing the association probe's IOKit owner. */
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "../supplicant/wpa2.h"
#include "../supplicant/data.h"
#include "../net80211/dhcp_probe.h"
#include "../rtl8188cu/tx.h"
#include "../control/wpa.h"
struct os_reltime;
#include "utils/eloop.h"
static unsigned char probe_pmk[32];
static struct dwa_wpa2 *supplicant;
static unsigned int probe_sequence=32,probe_keys,probe_sends,probe_m1,probe_m3;
static int probe_failed,probe_ok,probe_ended,probe_stopped,probe_abandon;
static int probe_data,probe_offer,probe_abandon_data;
static int probe_network;
static unsigned int network_seconds;
static struct dwa_data *probe_crypto;
static unsigned char probe_xid[4];
static unsigned int data_start,data_last,data_sends,data_rx,data_rejected,data_unicast,data_group;
static int data_open_guard(const char *);
static void wipe(void *v,unsigned int n)
{ volatile unsigned char *p=v;while(n--)*p++=0; }
static int read_pmk(const char *ssid)
{
    char secret[64],*terminal=0;unsigned int n=0;int c,ok=0;
    struct rlimit limit;limit.rlim_cur=0;limit.rlim_max=0;
    if(setrlimit(RLIMIT_CORE,&limit))return 0;
    memset(secret,0,sizeof(secret));
    if(isatty(STDIN_FILENO)) {
        terminal=getpass("WPA2 passphrase (not saved): ");
        if(terminal) { n=(unsigned int)strlen(terminal);if(n<=63)memcpy(secret,terminal,n);else n=64; }
    } else {
        while((c=getchar())!=EOF&&c!='\n') { if(n>=63||!c){n=64;break;}secret[n++]=(char)c; }
    }
    if(n>=8&&n<=63)ok=dwa_wpa2_pmk(secret,(const unsigned char *)ssid,strlen(ssid),probe_pmk)==0;
    if(terminal)wipe(terminal,(unsigned int)strlen(terminal));
    wipe(secret,sizeof(secret));
    if(!ok)fprintf(stderr,"Cannot read an 8..63 byte passphrase from standard input\n");
    return ok;
}
static int verify_input_pmk(void)
{
    unsigned char expected[32];int ok;
    if(isatty(STDIN_FILENO))return 0;
    ok=fread(expected,1,32,stdin)==32&&!memcmp(expected,probe_pmk,32);
    wipe(expected,sizeof(expected));
    printf("Independent input/PMK check: %s\n",ok?"PASS":"FAIL");
    if(!ok)wipe(probe_pmk,sizeof(probe_pmk));
    return ok;
}
static int refresh_after_prepare(const char *ssid)
{
    struct rtl_station candidate;unsigned int start=now_ms(),i,cck1,seen=0,newest=5001;
    /* CAM preparation precedes authentication and can outlive the original
     * beacon's freshness window. Require a new beacon from the same peer. */
    do {
        if(!receive(DWA_CONTROL_NEXT,0))return 0;
        if((rx.flags&DWA_CONTROL_FRAME)&&rx.frame.kind==DWA_FRAME_BEACON&&rx.frame.length>=36&&
           !memcmp(rx.frame.data+16,station.target.bssid,6)) {
            ++seen;if(rx.age_ms<newest)newest=rx.age_ms;
        }
        if((rx.flags&DWA_CONTROL_FRAME)&&rx.frame.kind==DWA_FRAME_BEACON&&rx.frame.length>=36&&
           rx.age_ms<=1000&&!memcmp(rx.frame.data+16,station.target.bssid,6)&&
           rtl_station_begin(&candidate,station.mac,rx.frame.data,rx.frame.length,
               (const unsigned char *)ssid,(unsigned int)strlen(ssid),rx.channel,rx.age_ms)==1) {
            cck1=0;for(i=0;i<candidate.rate_count;++i)if((candidate.rates[i]&127)==2)cck1=1;
            if(cck1){station=candidate;printf("Fresh beacon after CAM: wait_ms=%u age_ms=%lu seen=%u\n",now_ms()-start,(unsigned long)rx.age_ms,seen);return 1;}
        }
        usleep(5000);
    }while((unsigned int)(now_ms()-start)<3000);
    fprintf(stderr,"No fresh same-peer beacon after CAM preparation: seen=%u newest_ms=%u\n",seen,newest);return 0;
}
static int wpa_call(struct dwa_wpa_request *r)
{
    IOByteCount size=sizeof(tx);kern_return_t result;unsigned int op=r->operation;
    r->version=DWA_WPA_VERSION;memset(&tx,0,sizeof(tx));
    result=IOConnectMethodStructureIStructureO(connection,2,sizeof(*r),&size,r,&tx);
    wipe(r,sizeof(*r));
    if(result!=KERN_SUCCESS){fprintf(stderr,"WPA operation %u failed: 0x%08lx\n",op,(unsigned long)result);return -1;}
    return size==sizeof(tx)&&tx.version==DWA_ASSOC_VERSION?0:-1;
}
#include "network_runtime.inc"
static int probe_send(void *cookie,const unsigned char *f,size_t n)
{
    struct dwa_wpa_request r;unsigned int length,start=now_ms();int error;
    (void)cookie;memset(&r,0,sizeof(r));
    if(n>2048||rtl8188cu_eapol_frame(r.data,sizeof(r.data),&length,station.mac,
       station.target.bssid,probe_sequence++&4095,f,(unsigned int)n))return -1;
    r.operation=DWA_WPA_SEND;r.length=length;error=wpa_call(&r);if(error)return -1;
    do {
        if(!transmit(DWA_ASSOC_POLL,0,0))return -1;
        if(tx.result) {
            if(tx.result!=RTL_TX_SUCCESS||tx.failed)return -1;
            ++probe_sends;printf("EAPOL USB complete: message=%s bytes=%lu\n",(f[5]&2)?"M4":"M2",(unsigned long)n);
            return 0;
        }
        usleep(5000);
    }while((unsigned int)(now_ms()-start)<3500);
    return -1;
}
static int probe_install(void *cookie,int pair,unsigned int index,const unsigned char *key,
    const unsigned char *rsc,size_t rsc_n)
{
    struct dwa_wpa_request r;unsigned int slot=pair?4:index;(void)cookie;
    if(rsc_n>6||slot>4)return -1;
    memset(&r,0,sizeof(r));r.operation=DWA_WPA_KEY;r.length=16;r.pairwise=pair;r.index=index;
    memcpy(r.data,key,16);if(rsc_n)memcpy(r.rsc,rsc,rsc_n);
    if(wpa_call(&r))return -1;
    if(probe_crypto&&dwa_data_install_ccmp(probe_crypto,pair,index,key,rsc,rsc_n))return -1;
    probe_keys|=1U<<slot;printf("CCMP key installed and read back: %s slot=%u\n",pair?"pairwise":"group",slot);
    if(probe_abandon){puts("Key guard: exiting without userland cleanup");fflush(stdout);_exit(0);}
    return 0;
}
static int probe_port(void *cookie,int enabled)
{ (void)cookie;return enabled?-1:0; /* diagnostic never authorizes data */ }
static void probe_clear(void *cookie)
{
    (void)cookie;
    if(probe_crypto)dwa_data_clear(probe_crypto);
    if(probe_keys&&!probe_ended) {
        probe_ended=1;
        probe_stopped=transmit(DWA_ASSOC_END,0,0)&&(tx.flags&DWA_ASSOC_STOPPED)&&
            (tx.flags&DWA_ASSOC_DRAINED)&&tx.submitted==tx.completed&&!tx.failed;
    }
    probe_keys=0;
}
static void probe_failure(void *cookie,unsigned int reason)
{ (void)cookie;probe_failed=1;printf("WPA2 session failed: reason=%u\n",reason);eloop_terminate(); }
static int probe_complete(void *cookie)
{
    struct dwa_wpa_request r;(void)cookie;
    memset(&r,0,sizeof(r));r.operation=DWA_WPA_VERIFIED;
    if(wpa_call(&r))return -1;
    if(probe_data) {
        memset(&r,0,sizeof(r));r.operation=DWA_WPA_DATA_OPEN;
        if(wpa_call(&r)||dwa_data_port(probe_crypto,1))return -1;
        if(data_open_guard("reopening an active data window"))return -1;
        data_start=now_ms();
        if(probe_network&&network_start())return -1;
    }
    return 0;
}
static int data_open_guard(const char *stage)
{
    struct dwa_wpa_request r;struct dwa_assoc_reply reply;IOByteCount size=sizeof(reply);
    kern_return_t result;memset(&r,0,sizeof(r));memset(&reply,0,sizeof(reply));
    r.version=DWA_WPA_VERSION;r.operation=DWA_WPA_DATA_OPEN;
    result=IOConnectMethodStructureIStructureO(connection,2,sizeof(r),&size,&r,&reply);
    if(result!=kIOReturnNotPermitted)return -1;
    printf("CCMP gate rejected: %s\n",stage);return 0;
}
static int data_discover(void)
{
    unsigned char eth[DWA_DHCP_DISCOVER_SIZE];struct dwa_wpa_request r;
    size_t n=0;unsigned int seq=probe_sequence++&4095,start=now_ms();int error;
    memset(&r,0,sizeof(r));
    if(dwa_dhcp_discover(eth,sizeof(eth),station.mac,probe_xid,(now_ms()-data_start)/1000)||
       dwa_data_encrypt(probe_crypto,eth,sizeof(eth),r.data,sizeof(r.data),&n))return -1;
    /* Sequence number is masked from CCMP AAD. Keep the session's common
     * management/non-QoS sequence across unencrypted EAPOL and encrypted data. */
    r.data[22]=(unsigned char)(seq<<4);r.data[23]=(unsigned char)(seq>>4);
    r.operation=DWA_WPA_DATA_SEND;r.length=(rtl_u32)n;
    error=wpa_call(&r);wipe(eth,sizeof(eth));if(error)return -1;
    do {
        if(!transmit(DWA_ASSOC_POLL,0,0))return -1;
        if(tx.result) {
            if(tx.result!=RTL_TX_SUCCESS||tx.failed)return -1;
            ++data_sends;data_last=now_ms();printf("Encrypted DHCPDISCOVER USB complete: attempt=%u\n",data_sends);
            if(probe_abandon_data){puts("Data guard: exiting without userland cleanup");fflush(stdout);_exit(0);}
            return 0;
        }
        usleep(5000);
    }while((unsigned int)(now_ms()-start)<3500);
    return -1;
}
static int data_receive(void)
{
    unsigned char eth[DWA_DATA_ETH_MAX];size_t n=0;struct dwa_dhcp_offer offer;
    struct dwa_wpa_request r;int got;
    if((probe_network?dwa_data_decrypt_network:dwa_data_decrypt)(probe_crypto,rx.frame.data,rx.frame.length,eth,sizeof(eth),&n)){
        ++data_rejected;return 0;
    }
    ++data_rx;if(rx.frame.data[4]&1)++data_group;else ++data_unicast;
    if(probe_network){
        if(n>=14&&eth[12]==0x88&&eth[13]==0x8e){
            wipe(eth,sizeof(eth));puts("Authenticated key renewal: reconnecting with a fresh WPA2 session");return -1;
        }
        got=network_call(DWA_NET_RX,eth,(unsigned int)n);wipe(eth,sizeof(eth));return got?0:-1;
    }
    got=dwa_dhcp_parse_offer(eth,(unsigned int)n,station.mac,probe_xid,&offer)==0;
    wipe(eth,sizeof(eth));if(!got)return 0;
    memset(&r,0,sizeof(r));r.operation=DWA_WPA_DATA_OFFER;if(wpa_call(&r))return -1;
    printf("Authenticated DHCPOFFER: address=%u.%u.%u.%u server=%u.%u.%u.%u (no lease requested)\n",
        offer.address[0],offer.address[1],offer.address[2],offer.address[3],
        offer.server[0],offer.server[1],offer.server[2],offer.server[3]);
    probe_offer=1;return 1;
}
static void probe_tick(void *cookie,void *unused)
{
    unsigned int i;(void)cookie;(void)unused;
    for(i=0;i<(probe_network?32U:8U)&&!probe_failed;++i) {
        struct rtl_eapol_view view;
        if(!receive(DWA_CONTROL_NEXT,0)){probe_failure(0,16);break;}
        if(!(rx.flags&DWA_CONTROL_FRAME))break;
        if(rx.age_ms>DWA_FRAME_MAX_AGE_MS)continue;
        if(probe_data&&probe_ok&&rx.frame.kind==DWA_FRAME_CCMP) {
            int got=data_receive();if(got<0){probe_failure(0,16);break;}
            if(got){eloop_terminate();return;}continue;
        }
        if(rx.frame.kind==DWA_FRAME_MANAGEMENT) {
            rtl_station_receive(&station,rx.frame.data,rx.frame.length);
            if(station.state==RTL_STA_FAILED){
                printf("AP management rejection: subtype=0x%02x reason=%u\n",rx.frame.data[0],station.status);
                probe_failure(0,station.status);break;
            }
        }
        if(rx.frame.kind!=DWA_FRAME_EAPOL||rtl_eapol_from_80211(rx.frame.data,rx.frame.length,
            station.mac,station.target.bssid,&view))continue;
        if(view.is_m1)++probe_m1;
        if((view.key_info&0x01c8)==0x01c8)++probe_m3;
        dwa_wpa2_receive(supplicant,station.target.bssid,view.data,view.length);
        if(dwa_wpa2_handshake_verified(supplicant)) {
            probe_ok=1;if(!probe_data){eloop_terminate();return;}
        }
    }
    if(probe_network&&probe_ok&&!probe_failed) {
        if(network_tick()){probe_failure(0,16);return;}
        if(network_finished)return;
    } else if(probe_data&&probe_ok&&!probe_failed) {
        if((unsigned int)(now_ms()-data_start)>=9000){probe_failure(0,16);return;}
        if(data_sends<DWA_WPA_DATA_MAX_SEND&&(!data_sends||(unsigned int)(now_ms()-data_last)>=2000))
            if(data_discover()){probe_failure(0,16);return;}
    }
    if(!probe_failed&&eloop_register_timeout(0,5000,probe_tick,0,0))probe_failure(0,16);
}
static int run_wpa_probe(const char *ssid)
{
    struct dwa_wpa_request r;struct dwa_wpa2_ops ops;int initialized=0;unsigned int start=now_ms();
    memset(&r,0,sizeof(r));r.operation=DWA_WPA_ENTER;r.aid=station.aid;
    if(probe_data) {
        FILE *random=fopen("/dev/urandom","rb");int read_ok=0;
        if(random){read_ok=fread(probe_xid,1,sizeof(probe_xid),random)==sizeof(probe_xid);fclose(random);}
        if(!read_ok||!(probe_crypto=dwa_data_create(station.mac,station.target.bssid)))goto done;
    }
    if(wpa_call(&r))goto done;
    if(probe_data&&data_open_guard("opening before verified handshake"))goto done;
    printf("WPA hardware prepared: elapsed_ms=%u\n",(unsigned int)(now_ms()-start));
    if(eloop_init())goto done;initialized=1;
    if(probe_network&&eloop_register_signal_terminate(network_signal,0))goto done;
    memset(&ops,0,sizeof(ops));ops.send_eapol=probe_send;ops.install_ccmp=probe_install;
    ops.port=probe_port;ops.clear_keys=probe_clear;ops.failed=probe_failure;ops.handshake_complete=probe_complete;
    supplicant=dwa_wpa2_create(&ops,station.mac,station.target.bssid,
        (const unsigned char *)ssid,strlen(ssid),probe_pmk,station.target.rsn_ie,station.target.rsn_length);
    wipe(probe_pmk,sizeof(probe_pmk));
    if(!supplicant||dwa_wpa2_associated(supplicant)||eloop_register_timeout(0,0,probe_tick,0,0))goto done;
    eloop_run();
done:
    if(probe_network)network_stop();
    printf("WPA2 result: M1=%u M3=%u EAPOL_TX=%u handshake_verified=%d data_authorized=0\n",
        probe_m1,probe_m3,probe_sends,probe_ok);
    if(supplicant){dwa_wpa2_destroy(supplicant);supplicant=0;}
    if(probe_crypto){dwa_data_destroy(probe_crypto);probe_crypto=0;}
    if(probe_network)printf("Network result: interface=%s TX=%u authenticated_RX=%u rejected_RX=%u unicast_RX=%u group_RX=%u finished=%d\n",network_bsd,data_sends,data_rx,data_rejected,data_unicast,data_group,network_finished);
    else if(probe_data)printf("CCMP data result: TX=%u authenticated_RX=%u rejected_RX=%u unicast_RX=%u group_RX=%u dhcp_offer=%d network_interface=0\n",
        data_sends,data_rx,data_rejected,data_unicast,data_group,probe_offer);
    if(initialized)eloop_destroy();wipe(probe_pmk,sizeof(probe_pmk));
    return probe_ok&&!probe_failed&&(probe_network?network_finished:(!probe_data||probe_offer));
}
