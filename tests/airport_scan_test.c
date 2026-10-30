/* Run as root on Panther with an idle, non-associated prototype radio.
 * No injected APs: validates native RPCs against the actual scan engine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include "../src/control/protocol.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define WAIT ((IOReturn)0x20000108)
#define END ((IOReturn)0x20000109)
static IOReturn rpc(io_connect_t c,void *in,unsigned int bytes,void *out,unsigned int size)
{IOByteCount n=size;return IOConnectMethodStructureIStructureO(c,0,bytes,&n,in,out);}
int main(int argc,char **argv)
{
    io_service_t s,hw;io_connect_t c=0,owner=0;
    unsigned char request[35],next[6],out[52];unsigned int i,j,count=0,waits=0;
    IOReturn r;
    s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));CHECK(s);
    CHECK(IOServiceOpen(s,mach_task_self(),12,&c)==0);IOObjectRelease(s);
    memset(next,0,sizeof(next));next[0]=9;
    CHECK(rpc(c,next,5,out,50)==END);
    memset(request,0,sizeof(request));request[0]=8;
    CHECK(rpc(c,request,33,out,0)==kIOReturnBadArgument);
    CHECK(rpc(c,request,35,out,0)==kIOReturnBadArgument);
    CHECK(rpc(c,request,34,out,1)==kIOReturnBadArgument);
    request[33]=1;CHECK(rpc(c,request,34,out,0)==kIOReturnBadArgument);request[33]=0;
    request[1]='a';request[3]='b';
    CHECK(rpc(c,request,34,out,0)==kIOReturnBadArgument);request[1]=request[3]=0;
    CHECK(rpc(c,next,4,out,50)==kIOReturnBadArgument);
    CHECK(rpc(c,next,6,out,50)==kIOReturnBadArgument);
    CHECK(rpc(c,next,5,out,49)==kIOReturnBadArgument);
    CHECK(rpc(c,next,5,out,51)==kIOReturnBadArgument);
    hw=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("DWA121Diagnostic"));CHECK(hw);
    if(argc==2&&!strcmp(argv[1],"--cancel")){
        CFMutableDictionaryRef stop=CFDictionaryCreateMutable(0,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        CHECK(stop);sleep(3);CHECK(rpc(c,request,34,out,0)==0);
        CFDictionarySetValue(stop,CFSTR("RXStop"),kCFBooleanTrue);
        CHECK(IORegistryEntrySetCFProperties(hw,stop)==0);CFRelease(stop);
        CHECK(rpc(c,next,5,out,50)==END);
        next[0]=13;CHECK(rpc(c,next,1,out,0)==0);
        CHECK(rpc(c,request,34,out,0)==kIOReturnNotReady);
        IOObjectRelease(hw);IOServiceClose(c);
        puts("PASS: cancellation during scan, end sentinel on held handle, real power Off, restart rejected while Off");return 0;
    }
    CHECK(argc==1);
    CHECK(IOServiceOpen(hw,mach_task_self(),DWA_CONTROL_TYPE,&owner)==0);
    CHECK(rpc(c,request,34,out,0)==kIOReturnBusy);CHECK(IOServiceClose(owner)==0);owner=0;
    sleep(3); /* bypass the short coalescing window of any previous scan */
    CHECK(rpc(c,request,34,out,0)==0);
    /* Conversely the control owner cannot enter while a scan is pending. */
    CHECK(IOServiceOpen(hw,mach_task_self(),DWA_CONTROL_TYPE,&owner)!=0);
    IOObjectRelease(hw);
    for(i=0;i<240;++i){
        memset(out,0xa5,sizeof(out));r=rpc(c,next,5,out+1,50);
        CHECK(out[0]==0xa5&&out[51]==0xa5);
        if(r==WAIT){++waits;usleep(100000);continue;}
        CHECK(r==0||r==END);if(r==END)break;
        CHECK(out[17]==0&&out[18]>0&&out[18]<32);
        printf("Real BSSID=");for(j=7;j<13;++j)printf("%02x",out[j]);puts("");
        CHECK(++count<=16);next[4]=(unsigned char)count;
    }
    CHECK(i<240);printf("scan count=%u busy_polls=%u\n",count,waits);
    memset(next+1,255,4);CHECK(rpc(c,next,5,out,50)==END);
    memset(next+1,0,4);
    if(count){
        sleep(16);CHECK(rpc(c,next,5,out,50)==END);
    }
    CHECK(IOServiceClose(c)==0);
    puts("PASS: native sizes/canaries, no pre-scan cache, owner exclusion both ways, completion/end, expiry");
    return 0;
}
