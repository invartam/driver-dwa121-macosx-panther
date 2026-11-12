/* Panther-only hardware experiment. Never installed by the driver package.
 * Root, exactly one DWA-121 A1, no project driver attached. Standard device
 * descriptor reads and optional device-port reset, never controller reset. */
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
static int descriptor(IOUSBDeviceInterface182 **usb,const char *label)
{
    IOUSBDevRequestTO r;unsigned char bytes[18];IOReturn result;
    memset(&r,0,sizeof(r));memset(bytes,0,sizeof(bytes));
    r.bmRequestType=USBmakebmRequestType(kUSBIn,kUSBStandard,kUSBDevice);
    r.bRequest=kUSBRqGetDescriptor;r.wValue=kUSBDeviceDesc<<8;
    r.wLength=sizeof(bytes);r.pData=bytes;r.noDataTimeout=1000;r.completionTimeout=3000;
    result=(*usb)->DeviceRequestTO(usb,&r);
    printf("%s: GET_DESCRIPTOR result=%08lx bytes=%lu\n",label,(unsigned long)result,(unsigned long)r.wLenDone);fflush(stdout);
    if(result||r.wLenDone!=18||bytes[0]!=18||bytes[1]!=kUSBDeviceDesc||
       bytes[8]!=1||bytes[9]!=0x20||bytes[10]!=8||bytes[11]!=0x33)return 0;
    puts("Verified descriptor: VID=2001 PID=3308");return 1;
}
int main(int argc,char **argv)
{
    io_service_t service,device=0;io_iterator_t iterator=0;
    CFMutableDictionaryRef match;CFNumberRef v,p;int vid=0x2001,pid=0x3308,reset,count=0,ok=0;
    IOCFPlugInInterface **plugin=0;IOUSBDeviceInterface182 **usb=0;SInt32 score;IOReturn r;
    if(argc!=2||(strcmp(argv[1],"--descriptor")&&strcmp(argv[1],"--reset"))||geteuid()!=0){
        fputs("Root test only: usb-reset-probe --descriptor|--reset\n",stderr);return 1;}
    reset=!strcmp(argv[1],"--reset");
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("DWA121Diagnostic"));
    if(service){IOObjectRelease(service);fputs("Project driver attached; refusing USB experiment.\n",stderr);return 1;}
    match=IOServiceMatching("IOUSBDevice");v=CFNumberCreate(0,kCFNumberIntType,&vid);p=CFNumberCreate(0,kCFNumberIntType,&pid);
    if(!match||!v||!p)return 1;
    CFDictionarySetValue(match,CFSTR("idVendor"),v);CFDictionarySetValue(match,CFSTR("idProduct"),p);CFRelease(v);CFRelease(p);
    if(IOServiceGetMatchingServices(kIOMasterPortDefault,match,&iterator))return 1;
    while((service=IOIteratorNext(iterator))){++count;if(!device)device=service;else IOObjectRelease(service);}
    IOObjectRelease(iterator);
    if(count!=1){if(device)IOObjectRelease(device);fprintf(stderr,"Expected one DWA-121, found %d\n",count);return 2;}
    r=IOCreatePlugInInterfaceForService(device,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&plugin,&score);IOObjectRelease(device);
    if(r||!plugin)return 1;
    r=(*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID182),(LPVOID *)&usb);
    (*plugin)->Release(plugin);if(r||!usb)return 1;
    r=(*usb)->USBDeviceOpen(usb);printf("USBDeviceOpen=%08lx\n",(unsigned long)r);
    if(!r){
        ok=descriptor(usb,"before");
        if(ok&&reset){r=(*usb)->ResetDevice(usb);printf("ResetDevice=%08lx (descriptor verification still required)\n",(unsigned long)r);fflush(stdout);
            usleep(500000);ok=!r&&descriptor(usb,"after");}
        (*usb)->USBDeviceClose(usb);
    }
    (*usb)->Release(usb);return ok?0:1;
}
