/* Private Panther control ABI exerciser. No password or frame payload logging. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../control/protocol.h"
#include "../net80211/station.h"
#include "../rtl8188cu/tx.h"
static io_service_t service;
static io_connect_t connection;
static struct dwa_control_reply reply;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); goto fail; } } while(0)
static kern_return_t request(unsigned int operation,const unsigned char *bssid)
{
    struct dwa_control_request r; IOByteCount size=sizeof(reply); kern_return_t result;
    memset(&r,0,sizeof(r));
    r.version=DWA_CONTROL_VERSION; r.operation=operation;
    if(bssid) memcpy(r.bssid,bssid,6);
    memset(&reply,0,sizeof(reply));
    result=IOConnectMethodStructureIStructureO(connection,0,sizeof(r),&size,&r,&reply);
    if(result==KERN_SUCCESS && (size!=sizeof(reply) || reply.version!=DWA_CONTROL_VERSION ||
       reply.queued>DWA_FRAME_SLOTS || reply.channel<1 || reply.channel>11 ||
       reply.frame.length>DWA_FRAME_MAX || reply.age_ms>DWA_FRAME_MAX_AGE_MS)) return kIOReturnBadArgument;
    return result;
}
static int observe(unsigned int iterations,unsigned int *beacons)
{
    unsigned int i,management=0,eapol=0;
    for(i=0;i<iterations;++i) {
        if(request(DWA_CONTROL_NEXT,0)!=KERN_SUCCESS || !(reply.flags&DWA_CONTROL_RUNNING)) return 0;
        if(reply.flags&DWA_CONTROL_FRAME) {
            if(reply.frame.kind==DWA_FRAME_BEACON) ++*beacons;
            else if(reply.frame.kind==DWA_FRAME_MANAGEMENT) ++management;
            else if(reply.frame.kind==DWA_FRAME_EAPOL) ++eapol;
            else return 0;
        } else usleep(10000);
    }
    printf("RX bridge: beacons=%u management=%u EAPOL=%u dropped=%lu expired=%lu\n",
           *beacons,management,eapol,(unsigned long)reply.dropped,(unsigned long)reply.expired);
    return 1;
}
static int prepare(const char *ssid)
{
    struct rtl_station station; struct rtl_tx_packet packet;
    unsigned int i,j,cck1,age,length=(unsigned int)strlen(ssid);
    if(!length || length>32) return 0;
    for(i=0;i<1000;++i) {
        if(request(DWA_CONTROL_NEXT,0)!=KERN_SUCCESS || !(reply.flags&DWA_CONTROL_RUNNING)) return 0;
        if(!(reply.flags&DWA_CONTROL_FRAME)) { usleep(10000); continue; }
        if(reply.frame.kind!=DWA_FRAME_BEACON) continue;
        if(rtl_station_begin(&station,reply.mac,reply.frame.data,reply.frame.length,
            (const rtl_u8 *)ssid,length,reply.channel,reply.age_ms)!=1) continue;
        age=reply.age_ms; cck1=0;
        for(j=0;j<station.rate_count;++j) if((station.rates[j]&127)==2) cck1=1;
        if(!cck1) return 0;
        if(rtl8188cu_tx_prepare(&packet,station.tx,station.tx_length,station.mac,station.target.bssid,64)!=0 ||
           rtl8188cu_tx_prepare(&packet,station.tx,station.tx_length,station.mac,station.target.bssid,512)!=0) return 0;
        if(request(DWA_CONTROL_SELECT,station.target.bssid)!=KERN_SUCCESS ||
           !(reply.flags&DWA_CONTROL_SELECTED) || memcmp(reply.bssid,station.target.bssid,6)) return 0;
        printf("PASS: live beacon -> station AUTH request prepared; channel=%u age_ms=%u rates=%u frame=%u USB=%u endpoint=%u\n",
            station.target.channel,age,station.rate_count,station.tx_length,packet.length,packet.endpoint);
        puts("No frame transmitted; no association or WPA2 authentication performed.");
        rtl_station_disconnect(&station); memset(&packet,0,sizeof(packet));
        return 1;
    }
    fprintf(stderr,"No fresh compatible PSK/CCMP beacon for requested SSID on this channel\n");
    return 0;
}
int main(int argc,char **argv)
{
    io_iterator_t iterator=0; io_connect_t second=0; kern_return_t result;
    unsigned int beacons=0,i,mode; IOByteCount size; struct dwa_control_request bad;
    static const unsigned char peer[]={2,0x12,0x34,0x56,0x78,0x9a};
    pid_t child; int status;
    if(!((argc==2 && (!strcmp(argv[1],"watch") || !strcmp(argv[1],"selftest") || !strcmp(argv[1],"die") ||
                     !strcmp(argv[1],"drop") || !strcmp(argv[1],"stop-check"))) ||
         (argc==3 && !strcmp(argv[1],"prepare")))) {
        fprintf(stderr,"Usage: sudo dwa121-control watch|selftest|die|drop|stop-check|prepare SSID\n"); return 1;
    }
    CHECK(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("DWA121Diagnostic"),&iterator)==KERN_SUCCESS);
    service=IOIteratorNext(iterator); IOObjectRelease(iterator); CHECK(service!=0);
    result=IOServiceOpen(service,mach_task_self(),DWA_CONTROL_TYPE,&connection);
    if(result!=KERN_SUCCESS) { fprintf(stderr,"Control open rejected: 0x%08lx\n",(unsigned long)result); goto fail; }
    CHECK(request(DWA_CONTROL_STATUS,0)==KERN_SUCCESS);
    CHECK((reply.flags&DWA_CONTROL_RUNNING) && !(reply.flags&DWA_CONTROL_SELECTED));
    CHECK(!reply.delivered);
    if(!strcmp(argv[1],"prepare")) { CHECK(prepare(argv[2])); goto done; }
    if(!strcmp(argv[1],"stop-check")) {
        CFMutableDictionaryRef stop=CFDictionaryCreateMutable(kCFAllocatorDefault,0,
            &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        CHECK(stop!=0);
        CFDictionarySetValue(stop,CFSTR("RXStop"),kCFBooleanTrue);
        result=IORegistryEntrySetCFProperties(service,stop); CFRelease(stop);
        CHECK(result==KERN_SUCCESS);
        CHECK(request(DWA_CONTROL_STATUS,0)==kIOReturnNotReady);
        CHECK(IOServiceOpen(service,mach_task_self(),DWA_CONTROL_TYPE,&second)!=KERN_SUCCESS && !second);
        puts("PASS: hardware stop revokes live client and rejects reopen"); goto done;
    }
    if(!strcmp(argv[1],"die") || !strcmp(argv[1],"drop")) {
        CHECK(request(DWA_CONTROL_SELECT,peer)==KERN_SUCCESS);
        if(!strcmp(argv[1],"drop")) {
            CHECK(setuid(501)==0);
            CHECK(request(DWA_CONTROL_STATUS,0)!=KERN_SUCCESS);
        }
        _exit(0); /* deliberately abandon the Mach connection */
    }
    if(!strcmp(argv[1],"watch")) { CHECK(observe(1000,&beacons)); goto done; }
    result=IOServiceOpen(service,mach_task_self(),DWA_CONTROL_TYPE,&second);
    CHECK(result!=KERN_SUCCESS && !second);
    memset(&bad,0,sizeof(bad)); bad.version=1; bad.operation=DWA_CONTROL_STATUS;
    size=sizeof(reply);
    CHECK(IOConnectMethodStructureIStructureO(connection,1,sizeof(bad),&size,&bad,&reply)!=KERN_SUCCESS);
    size=sizeof(reply);
    CHECK(IOConnectMethodStructureIStructureO(connection,0,sizeof(bad)-1,&size,&bad,&reply)!=KERN_SUCCESS);
    size=sizeof(reply)-1;
    CHECK(IOConnectMethodStructureIStructureO(connection,0,sizeof(bad),&size,&bad,&reply)!=KERN_SUCCESS);
    bad.version=2; size=sizeof(reply);
    CHECK(IOConnectMethodStructureIStructureO(connection,0,sizeof(bad),&size,&bad,&reply)!=KERN_SUCCESS);
    CHECK(request(DWA_CONTROL_SELECT,peer)==KERN_SUCCESS);
    CHECK((reply.flags&DWA_CONTROL_SELECTED) && !memcmp(reply.bssid,peer,6) && !reply.queued);
    CHECK(request(DWA_CONTROL_SELECT,reply.mac)!=KERN_SUCCESS);
    CHECK(request(DWA_CONTROL_STATUS,0)==KERN_SUCCESS && (reply.flags&DWA_CONTROL_SELECTED));
    CHECK(observe(400,&beacons) && beacons>0);
    CHECK(request(DWA_CONTROL_RESET,0)==KERN_SUCCESS);
    CHECK(!(reply.flags&DWA_CONTROL_SELECTED) && !reply.queued && !reply.delivered);
    /* An idle reader fills its bounded queue; old frames must never escape. */
    sleep(7);
    CHECK(request(DWA_CONTROL_NEXT,0)==KERN_SUCCESS);
    CHECK(reply.expired>0 && reply.dropped>0 && reply.queued<=16);
    printf("PASS: exclusive/root ABI, invalid requests, selected peer, real beacons, queue overflow/expiry/reset\n");
    CHECK(IOServiceClose(connection)==KERN_SUCCESS); connection=0;
    for(mode=0;mode<2;++mode) {
        child=fork(); CHECK(child>=0);
        if(child==0) {
            execl(argv[0],argv[0],mode?"drop":"die",(char *)0); _exit(127);
        }
        CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
        for(i=0;i<200;++i) {
            if(IOServiceOpen(service,mach_task_self(),DWA_CONTROL_TYPE,&connection)==KERN_SUCCESS) break;
            usleep(10000);
        }
        CHECK(connection!=0);
        CHECK(request(DWA_CONTROL_STATUS,0)==KERN_SUCCESS && !(reply.flags&DWA_CONTROL_SELECTED));
        CHECK(!reply.delivered && !reply.expired && !reply.dropped);
        CHECK(IOServiceClose(connection)==KERN_SUCCESS); connection=0;
    }
    printf("PASS: process death releases exclusive ownership and wipes session; privilege rechecked after setuid\n");
done:
    if(connection) IOServiceClose(connection);
    IOObjectRelease(service); return 0;
fail:
    if(second) IOServiceClose(second);
    if(connection) IOServiceClose(connection);
    if(service) IOObjectRelease(service);
    return 1;
}
