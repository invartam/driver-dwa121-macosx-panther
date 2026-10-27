/* Shared association engine: DWA_WPA2_PROBE adds WPA2; DWA_NETWORK_BUILD
 * connects the verified session to the BSD Ethernet interface and DHCP. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "../control/association.h"
#include "../net80211/station.h"
#include "../net80211/eapol.h"
#include "../usb/tx_lifecycle.h"
static io_connect_t connection;
static struct dwa_control_reply rx;
static struct dwa_assoc_reply tx;
static struct rtl_station station;
static unsigned int now_ms(void)
{
    mach_timebase_info_data_t info;
    unsigned long long ticks,ns;
    mach_timebase_info(&info);
    ticks=mach_absolute_time();
    /* Panther PPC may report an unreduced 1e9/frequency ratio. Divide first
     * without losing the remainder, avoiding overflow after hours of uptime. */
    ns=(ticks/info.denom)*info.numer+(ticks%info.denom)*info.numer/info.denom;
    return (unsigned int)(ns/1000000ULL);
}
static int receive(unsigned int op,const rtl_u8 *ap)
{
    struct dwa_control_request r; IOByteCount size=sizeof(rx);
    memset(&r,0,sizeof(r)); r.version=DWA_CONTROL_VERSION; r.operation=op;
    if(ap) memcpy(r.bssid,ap,6);
    memset(&rx,0,sizeof(rx));
    return IOConnectMethodStructureIStructureO(connection,0,sizeof(r),&size,&r,&rx)==KERN_SUCCESS &&
        size==sizeof(rx) && rx.version==DWA_CONTROL_VERSION && (rx.flags&DWA_CONTROL_RUNNING) &&
        rx.frame.length<=DWA_FRAME_MAX && rx.age_ms<=DWA_FRAME_MAX_AGE_MS;
}
static int transmit(unsigned int op,const rtl_u8 *f,unsigned int length)
{
    struct dwa_assoc_request r; IOByteCount size=sizeof(tx); kern_return_t result;
    memset(&r,0,sizeof(r)); r.version=DWA_ASSOC_VERSION; r.operation=op;
    if(op==DWA_ASSOC_BEGIN) { r.channel=station.target.channel; memcpy(r.bssid,station.target.bssid,6); }
    if(f) { if(length>512) return 0; r.length=length; memcpy(r.frame,f,length); }
    memset(&tx,0,sizeof(tx));
    result=IOConnectMethodStructureIStructureO(connection,1,sizeof(r),&size,&r,&tx);
    if(result!=KERN_SUCCESS) {
        fprintf(stderr,"Association operation %u failed: 0x%08lx\n",op,(unsigned long)result); return 0;
    }
    return size==sizeof(tx) && tx.version==DWA_ASSOC_VERSION;
}
static int send_station(void)
{
    unsigned int start=now_ms();
    if(!transmit(DWA_ASSOC_SEND,station.tx,station.tx_length)) return 0;
    do {
        if(!transmit(DWA_ASSOC_POLL,0,0)) return 0;
        if(tx.result) {
            printf("USB management complete: state=%u attempt=%u length=%u result=%lu\n",
                station.state,station.attempts,station.tx_length,(unsigned long)tx.result);
            return tx.result==RTL_TX_SUCCESS && !tx.failed;
        }
        usleep(5000);
    } while((unsigned int)(now_ms()-start)<3500);
    return 0;
}
#ifdef DWA_WPA2_PROBE
#include "wpa_probe.inc"
#ifdef DWA_NETWORK_BUILD
#include "../config/config.h"
#endif
#endif
int main(int argc,char **argv)
{
    io_iterator_t it=0; io_service_t service=0; unsigned int start,deadline,j,cck1,age=0;
    unsigned int management=0,eapol=0,m1=0,other=0,rejected=0,ssid_n;
    int require_m1=0;
    int found=0,ok=0,begun=0,stopped=0,action,guard=0,guard_ok=0;
#ifdef DWA_WPA2_PROBE
    int wpa_ok=0,check_pmk=0,configured=0;
#ifdef DWA_NETWORK_BUILD
    char configured_ssid[33];struct dwa_config config;
    if(geteuid()!=0){fprintf(stderr,"Run dwa121-connect with sudo.\n");return 1;}
    if(argc==3&&!strcmp(argv[1],"--config")){
        struct rlimit limit;limit.rlim_cur=limit.rlim_max=0;
        if(setrlimit(RLIMIT_CORE,&limit)||!dwa_config_read(argv[2],&config)){fprintf(stderr,"Configuration absente ou non protegee (root, 0600).\n");return 1;}
        memcpy(configured_ssid,config.ssid,config.ssid_length);configured_ssid[config.ssid_length]=0;
        memcpy(probe_pmk,config.pmk,32);dwa_config_clear(&config,sizeof(config));
        argv[1]=configured_ssid;argc=2;configured=1;
    }
#endif
    (void)m1;(void)other;(void)rejected;
    if(argc<2||argc>6||!strlen(argv[1])||strlen(argv[1])>32){
#ifdef DWA_NETWORK_BUILD
        fprintf(stderr,"Usage: sudo dwa121-connect SSID [--seconds N] | --config FILE\n");
#else
        fprintf(stderr,"Usage: sudo dwa121-wpa2 SSID [--verify-pmk-stdin] [--abandon-after-key | --dhcp-discover [--abandon-after-data]] (passphrase from terminal or stdin)\n");
#endif
        return 1;
    }
#ifdef DWA_NETWORK_BUILD
    probe_network=probe_data=1;
#endif
    for(j=2;j<(unsigned int)argc;++j) {
        if(!strcmp(argv[j],"--seconds")&&probe_network&&j+1<(unsigned int)argc){
            char *end=0;unsigned long n=strtoul(argv[++j],&end,10);
            if(!end||*end||n>86400||!n){fprintf(stderr,"Invalid session duration\n");return 1;}network_seconds=(unsigned int)n;
        }
        else if(probe_network){fprintf(stderr,"Unknown network option\n");return 1;}
        else if(!strcmp(argv[j],"--verify-pmk-stdin")&&!check_pmk)check_pmk=1;
        else if(!strcmp(argv[j],"--abandon-after-key")&&!probe_abandon)probe_abandon=1;
        else if(!strcmp(argv[j],"--dhcp-discover")&&!probe_data)probe_data=1;
        else if(!strcmp(argv[j],"--abandon-after-data")&&!probe_abandon_data)probe_abandon_data=1;
        else {fprintf(stderr,"Unknown or repeated probe option\n");return 1;}
    }
    if(probe_abandon&&probe_data){fprintf(stderr,"Key guard and data discovery are separate probes\n");return 1;}
    if(probe_abandon_data&&!probe_data){fprintf(stderr,"Data guard requires --dhcp-discover\n");return 1;}
    if(!configured&&!read_pmk(argv[1]))return 1;
    if(check_pmk&&!verify_input_pmk())return 1;
    ssid_n=(unsigned int)strlen(argv[1]);
    (void)require_m1;
#else
    if(argc==3) {
        if(!strcmp(argv[2],"--deadline")) guard=1;
        if(!strcmp(argv[2],"--abandon")) guard=2;
        if(!strcmp(argv[2],"--drop")) guard=3;
        if(!strcmp(argv[2],"--require-m1")) require_m1=1;
    }
    if(!((argc==2)||(argc==3 && (guard||require_m1))) || !(ssid_n=(unsigned int)strlen(argv[1])) || ssid_n>32) {
        fprintf(stderr,"Usage: sudo dwa121-associate SSID [--require-m1|--deadline|--abandon|--drop] (requires AssociationTXEnabled)\n"); return 1;
    }
#endif
    start=now_ms();
    do {
        if(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("DWA121Diagnostic"),&it)!=KERN_SUCCESS)goto done;
        service=IOIteratorNext(it);IOObjectRelease(it);
        if(service&&IOServiceOpen(service,mach_task_self(),DWA_CONTROL_TYPE,&connection)==KERN_SUCCESS)break;
        if(service){IOObjectRelease(service);service=0;}connection=0;
#ifdef DWA_NETWORK_BUILD
        if((unsigned int)(now_ms()-start)<20000){usleep(250000);continue;}
#endif
        fprintf(stderr,"Control open rejected\n");goto done;
    }while(1);
    start=now_ms();
    while((unsigned int)(now_ms()-start)<10000) {
        if(!receive(DWA_CONTROL_NEXT,0)) goto done;
        if(!(rx.flags&DWA_CONTROL_FRAME)) { usleep(5000); continue; }
        if(rx.frame.kind!=DWA_FRAME_BEACON) continue;
        if(rtl_station_begin(&station,rx.mac,rx.frame.data,rx.frame.length,
            (const rtl_u8 *)argv[1],ssid_n,rx.channel,rx.age_ms)!=1) continue;
        cck1=0; for(j=0;j<station.rate_count;++j) if((station.rates[j]&127)==2) cck1=1;
        if(!cck1) continue;
        age=rx.age_ms; found=1; break;
    }
    if(!found) { fprintf(stderr,"No fresh compatible beacon on current channel\n"); goto done; }
    if(!receive(DWA_CONTROL_SELECT,station.target.bssid)) goto done;
    if(!transmit(DWA_ASSOC_BEGIN,0,0)) goto done;
    begun=1; start=now_ms();
#ifdef DWA_WPA2_PROBE
    if(!refresh_after_prepare(argv[1]))goto done;
    age=rx.age_ms;
#endif
    printf("Management TX started: channel=%u beacon_age_ms=%u\n",station.target.channel,age);
    if(guard==2) { puts("Abandoning live session without sending a frame"); fflush(stdout); _exit(0); }
    if(guard==1) {
        sleep(12);
        guard_ok=!receive(DWA_CONTROL_STATUS,0); begun=0; goto done;
    }
    if(guard==3) {
        guard_ok=setuid(501)==0 && !transmit(DWA_ASSOC_POLL,0,0);
        begun=0; goto done; /* close still cleans up after loss of privilege */
    }
    if(!send_station()) goto done;
    deadline=now_ms();
    while((unsigned int)(now_ms()-start)<9000) {
        if(!receive(DWA_CONTROL_NEXT,0)) goto done;
        if(rx.flags&DWA_CONTROL_FRAME) {
            if(rx.frame.kind==DWA_FRAME_MANAGEMENT) {
                unsigned int previous=station.state;
                ++management;
                action=rtl_station_receive(&station,rx.frame.data,rx.frame.length);
                printf("Management RX: subtype=0x%02x length=%lu state=%u->%u status=%u\n",
                    rx.frame.data[0],(unsigned long)rx.frame.length,previous,station.state,station.status);
                if(station.state==RTL_STA_FAILED) goto done;
                if(action==1) { if(!send_station()) goto done; deadline=now_ms(); }
                if(station.state==RTL_STA_KEYS) { ok=1; break; }
            }
        } else usleep(5000);
        if((unsigned int)(now_ms()-deadline)>=1000) {
            if(rtl_station_timeout(&station)!=1 || !send_station()) goto done;
            deadline=now_ms();
        }
    }
    /* Decode the queue's full 802.11 frame; M1 has no MIC. Never log nonce,
     * replay counter or key data, and never infer WPA authorization from M1. */
#ifdef DWA_WPA2_PROBE
    if(ok){wpa_ok=run_wpa_probe(argv[1]);eapol=probe_m1+probe_m3;}
#else
    if(ok) {
        deadline=now_ms();
        while((unsigned int)(now_ms()-deadline)<(require_m1?1500U:300U) && (unsigned int)(now_ms()-start)<9500) {
            if(!receive(DWA_CONTROL_NEXT,0)) { ok=0; break; }
            if((rx.flags&DWA_CONTROL_FRAME) && rx.frame.kind==DWA_FRAME_EAPOL) {
                struct rtl_eapol_view view;
                ++eapol;
                if(rtl_eapol_from_80211(rx.frame.data,rx.frame.length,station.mac,station.target.bssid,&view)) ++rejected;
                else if(view.is_m1) ++m1;
                else ++other;
            }
            else usleep(5000);
        }
    }
#endif
done:
#ifdef DWA_WPA2_PROBE
    if(probe_ended){begun=0;stopped=probe_stopped;}
    wipe(probe_pmk,sizeof(probe_pmk));
#endif
    if(begun) stopped=transmit(DWA_ASSOC_END,0,0) && (tx.flags&DWA_ASSOC_STOPPED) &&
        (tx.flags&DWA_ASSOC_DRAINED) && tx.submitted==tx.completed && !tx.failed;
    if(connection) IOServiceClose(connection);
    if(service) IOObjectRelease(service);
    printf("Association probe: associated=%d state=%u status=%u AID=%u management=%u EAPOL=%u USB=%lu/%lu bytes=%lu stopped=%d\n",
        ok,station.state,station.status,station.aid,management,eapol,
        (unsigned long)tx.completed,(unsigned long)tx.submitted,(unsigned long)tx.bytes,stopped);
#ifdef DWA_WPA2_PROBE
    if(probe_network)puts("WPA2 network session stopped; radio and keys released.");
    else puts(probe_data?"Bounded encrypted DHCP discovery only; no network interface or lease. Radio session ends here.":"Initial WPA2 handshake probe only; data port stayed closed and the radio session ends here.");
#else
    puts("No WPA2 handshake or data traffic; radio session ends after this probe.");
    printf("EAPOL inspection: M1=%u other=%u rejected=%u required=%d (M1 is unauthenticated)\n",m1,other,rejected,require_m1);
#endif
    rtl_station_disconnect(&station);
    if(guard) { printf("Guard probe: mode=%d passed=%d\n",guard,guard_ok); return guard_ok?0:1; }
#ifdef DWA_WPA2_PROBE
    return ok&&stopped&&wpa_ok?0:1;
#else
    return ok && stopped && (!require_m1 || m1) ? 0 : 1;
#endif
}
