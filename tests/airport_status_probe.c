/* Read-only diagnostic: status contains identities, never credentials. */
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
extern int WirelessAttach(void **,unsigned int);
extern int WirelessGetInfo2(void *,void *);
extern int WirelessDetach(void *);
int main(void)
{
    io_service_t service;io_connect_t c=0;void *ref=0;
    unsigned char op=18,raw[56],native[64],unused=0;IOByteCount n=56;unsigned int i;kern_return_t r;
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));
    if(!service)return 1;r=IOServiceOpen(service,mach_task_self(),12,&c);IOObjectRelease(service);if(r)return 1;
    memset(raw,0,sizeof(raw));r=IOConnectMethodStructureIStructureO(c,0,1,&n,&op,raw);
    printf("raw18 result=%08x bytes=%lu data=",r,(unsigned long)n);for(i=0;i<56;++i)printf("%02x",raw[i]);puts("");
    {
        unsigned char request[10]={3,0,0,0,0,0,0,0,92,0},quality[14];IOByteCount bytes;
        memset(quality,0xa5,sizeof(quality));bytes=12;
        r=IOConnectMethodStructureIStructureO(c,0,9,&bytes,request,quality+1);
        assert(quality[0]==0xa5&&quality[13]==0xa5);
        assert(r==KERN_SUCCESS&&bytes==12);for(i=1;i<13;++i)assert(!quality[i]);
        bytes=12;assert(IOConnectMethodStructureIStructureO(c,0,8,&bytes,request,quality+1)!=KERN_SUCCESS);
        bytes=12;assert(IOConnectMethodStructureIStructureO(c,0,10,&bytes,request,quality+1)!=KERN_SUCCESS);
        bytes=11;assert(IOConnectMethodStructureIStructureO(c,0,9,&bytes,request,quality+1)!=KERN_SUCCESS);
        request[8]=93;bytes=12;assert(IOConnectMethodStructureIStructureO(c,0,9,&bytes,request,quality+1)!=KERN_SUCCESS);
        puts("PASS: read-only quality bounds and unavailable metrics");
    }
    op=13;n=0;r=IOConnectMethodStructureIStructureO(c,0,1,&n,&op,&unused);printf("raw13=%08x\n",r);IOServiceClose(c);
    r=WirelessAttach(&ref,0);printf("attach=%08x\n",r);if(r)return 1;
    memset(native,0,sizeof(native));r=WirelessGetInfo2(ref,native);printf("info2=%08x data=",r);for(i=0;i<64;++i)printf("%02x",native[i]);puts("");
    WirelessDetach(ref);return r?1:0;
}
