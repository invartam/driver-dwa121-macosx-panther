/* Signatures reconstructed from the installed Panther PPC framework.
 * This calls Apple's original framework, not a replacement implementation. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <mach/message.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
extern int WirelessIsAvailable(void);
extern int WirelessAttach(void **,unsigned int);
extern int WirelessGetPower(void *,unsigned char *);
extern int WirelessGetEnabled(void *,unsigned char *);
extern int WirelessGetInfo2(void *,void *);
extern int WirelessDetach(void *);
extern int WirelessScan(void *,CFArrayRef *,unsigned int);
static int checkTransport(unsigned char power,unsigned char enabled,const unsigned char *native)
{
    io_service_t service,provider=0,controller=0;io_connect_t c=0;CFTypeRef state;
    unsigned char in[2]={13,0},out=0,status[58],mac[6];IOByteCount n=0;IOReturn r;
    int expected,failed=0;
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));
    if(!service)return 1;
    if(IORegistryEntryGetParentEntry(service,kIOServicePlane,&controller)){IOObjectRelease(service);return 1;}
    if(IORegistryEntryGetParentEntry(controller,kIOServicePlane,&provider)){IOObjectRelease(controller);IOObjectRelease(service);return 1;}
    IOObjectRelease(controller);
    state=IORegistryEntryCreateCFProperty(provider,CFSTR("MACAddress"),0,0);
    if(!state||CFGetTypeID(state)!=CFDataGetTypeID()||CFDataGetLength(state)!=6){
        if(state)CFRelease(state);IOObjectRelease(provider);IOObjectRelease(service);return 1;
    }
    memcpy(mac,CFDataGetBytePtr(state),6);CFRelease(state);
    state=IORegistryEntryCreateCFProperty(provider,CFSTR("RadioRunning"),kCFAllocatorDefault,0);
    IOObjectRelease(provider);
    if(!state){IOObjectRelease(service);return 1;}
    expected=CFEqual(state,kCFBooleanTrue)?3:0;CFRelease(state);
    if(power!=(expected?1:0)||enabled!=(expected?1:0))failed=1;
    r=IOServiceOpen(service,mach_task_self(),12,&c);IOObjectRelease(service);
    if(r)return 1;
    r=IOConnectMethodStructureIStructureO(c,0,1,&n,in,&out);
    printf("Raw command13=0x%08x expected=%d from RadioRunning\n",r,expected);
    if(r!=(IOReturn)expected)failed=1;
    in[0]=255;n=0;
    r=IOConnectMethodStructureIStructureO(c,0,1,&n,in,&out);
    if(r!=kIOReturnUnsupported)failed=1;
    in[0]=13;n=0;
    r=IOConnectMethodStructureIStructureO(c,0,2,&n,in,&out);
    if(r!=kIOReturnBadArgument)failed=1;
    n=1;r=IOConnectMethodStructureIStructureO(c,0,1,&n,in,&out);
    if(r!=kIOReturnBadArgument)failed=1;
    n=0;r=IOConnectMethodStructureIStructureO(c,1,1,&n,in,&out);
    if(r!=kIOReturnUnsupported)failed=1;
    memset(status,0xa5,sizeof(status));in[0]=18;n=56;
    r=IOConnectMethodStructureIStructureO(c,0,1,&n,in,status);
    if(r||n!=56||memcmp(status+40,mac,6)||memcmp(native+58,mac,6)||status[56]!=0xa5||status[57]!=0xa5)failed=1;
    /* This milestone deliberately reports unknown association, not fake RSSI. */
    if(status[4]||status[5]!=1||native[10]||native[11]!=1)failed=1;
    if(status[2]||status[3]||native[8]||native[9]||native[14]||native[15]!=power)failed=1;
    n=55;r=IOConnectMethodStructureIStructureO(c,0,1,&n,in,status);
    if(r!=kIOReturnBadArgument)failed=1;
    n=57;r=IOConnectMethodStructureIStructureO(c,0,1,&n,in,status);
    if(r!=kIOReturnBadArgument)failed=1;
    n=56;r=IOConnectMethodStructureIStructureO(c,0,2,&n,in,status);
    if(r!=kIOReturnBadArgument)failed=1;
    IOServiceClose(c);
    printf("Transport and rejection checks: %s\n",failed?"FAIL":"PASS");
    return failed;
}
static int holdUntilDetached(void)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));
    io_connect_t c=0;IOReturn r;unsigned char in=13,out=0;IOByteCount n;int i,ok=0;
    if(!service)return 1;
    r=IOServiceOpen(service,mach_task_self(),12,&c);IOObjectRelease(service);
    if(r)return 1;
    puts("HOLD: client opened");fflush(stdout);
    for(i=0;i<30;++i){
        n=0;r=IOConnectMethodStructureIStructureO(c,0,1,&n,&in,&out);
        if(r==kIOReturnNotAttached||r==kIOReturnNotOpen||r==kIOReturnNoDevice||r==MACH_SEND_INVALID_DEST){ok=1;break;}
        if(r!=0&&r!=3)break;
        sleep(1);
    }
    printf("HOLD: detached=%d RPC=0x%08x; keeping handle for 10 seconds\n",ok,r);fflush(stdout);
    sleep(10);IOServiceClose(c);return ok?0:1;
}
static int scan(void)
{
    void *ref=0;CFArrayRef results=0;int r,failed=0;CFIndex i;
    r=WirelessAttach(&ref,0);if(r||!ref)return 1;
    r=WirelessScan(ref,&results,0);
    printf("WirelessScan=0x%08x count=%ld\n",r,results?(long)CFArrayGetCount(results):-1L);
    if(r)failed=1;
    if(results){
        for(i=0;i<CFArrayGetCount(results);++i){
            CFDataRef d=(CFDataRef)CFArrayGetValueAtIndex(results,i);
            const unsigned char *p;unsigned int n,j;
            if(CFGetTypeID(d)!=CFDataGetTypeID()||CFDataGetLength(d)!=50){failed=1;continue;}
            p=CFDataGetBytePtr(d);n=((unsigned int)p[16]<<8)|p[17];
            if(n>32){failed=1;continue;}
            printf("BSS %ld channel=%u BSSID=%02x:%02x:%02x:%02x:%02x:%02x caps=%02x%02x SSID[%u]=",
                (long)i,((unsigned int)p[0]<<8)|p[1],p[6],p[7],p[8],p[9],p[10],p[11],p[14],p[15],n);
            for(j=0;j<n;++j)printf("%02x",p[18+j]);puts("");
        }
        CFRelease(results);
    }
    if(WirelessDetach(ref))failed=1;return failed;
}
int main(int argc,char **argv)
{
    void *ref=0;unsigned char power=255,enabled=255,info[66];int r,p,e,d,i;
    if(argc==2&&!strcmp(argv[1],"--scan"))return scan();
    if(argc==2&&!strcmp(argv[1],"--hold-until-detached"))return holdUntilDetached();
    if(argc!=1)return 1;
    printf("Apple80211 WirelessIsAvailable=%d\n",WirelessIsAvailable());
    r=WirelessAttach(&ref,0);printf("WirelessAttach=0x%08x\n",r);
    if(r||!ref)return 1;
    p=WirelessGetPower(ref,&power);e=WirelessGetEnabled(ref,&enabled);
    printf("WirelessGetPower=0x%08x power=%u\n",p,power);
    printf("WirelessGetEnabled=0x%08x enabled=%u\n",e,enabled);
    memset(info,0xa5,sizeof(info));i=WirelessGetInfo2(ref,info);
    printf("WirelessGetInfo2=0x%08x radio=%u status=%u (unknown)\n",i,info[15],info[9]);
    if(info[64]!=0xa5||info[65]!=0xa5)i=1;
    d=WirelessDetach(ref);printf("WirelessDetach=0x%08x\n",d);
    return p||e||d||i||power>1||enabled>1||checkTransport(power,enabled,info);
}
