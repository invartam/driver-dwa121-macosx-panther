/* Signatures reconstructed from the installed Panther PPC framework.
 * This calls Apple's original framework, not a replacement implementation. */
#include <stdio.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
extern int WirelessIsAvailable(void);
extern int WirelessAttach(void **,unsigned int);
extern int WirelessGetPower(void *,unsigned char *);
extern int WirelessGetEnabled(void *,unsigned char *);
extern int WirelessDetach(void *);
static int checkTransport(unsigned char power,unsigned char enabled)
{
    io_service_t service,provider=0;io_connect_t c=0;CFTypeRef state;
    unsigned char in[2]={13,0},out=0;IOByteCount n=0;IOReturn r;
    int expected,failed=0;
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));
    if(!service)return 1;
    if(IORegistryEntryGetParentEntry(service,kIOServicePlane,&provider)){IOObjectRelease(service);return 1;}
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
    IOServiceClose(c);
    printf("Transport and rejection checks: %s\n",failed?"FAIL":"PASS");
    return failed;
}
int main(void)
{
    void *ref=0;unsigned char power=255,enabled=255;int r,p,e,d;
    printf("Apple80211 WirelessIsAvailable=%d\n",WirelessIsAvailable());
    r=WirelessAttach(&ref,0);printf("WirelessAttach=0x%08x\n",r);
    if(r||!ref)return 1;
    p=WirelessGetPower(ref,&power);e=WirelessGetEnabled(ref,&enabled);
    printf("WirelessGetPower=0x%08x power=%u\n",p,power);
    printf("WirelessGetEnabled=0x%08x enabled=%u\n",e,enabled);
    d=WirelessDetach(ref);printf("WirelessDetach=0x%08x\n",d);
    return p||e||d||power>1||enabled>1||checkTransport(power,enabled);
}
