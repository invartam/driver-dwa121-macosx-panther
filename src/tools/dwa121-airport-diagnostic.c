/* Read-only, allowlisted project state. Never opens a profile or dumps keys. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
static io_service_t device(void)
{
    CFMutableDictionaryRef d=IOServiceMatching("IOUSBDevice");int vid=0x2001,pid=0x3308;
    CFNumberRef v=CFNumberCreate(0,kCFNumberIntType,&vid),p=CFNumberCreate(0,kCFNumberIntType,&pid);
    if(!d||!v||!p){if(d)CFRelease(d);if(v)CFRelease(v);if(p)CFRelease(p);return 0;}
    CFDictionarySetValue(d,CFSTR("idVendor"),v);CFDictionarySetValue(d,CFSTR("idProduct"),p);
    CFRelease(v);CFRelease(p);return IOServiceGetMatchingService(kIOMasterPortDefault,d);
}
static void properties(io_service_t s,const char **names)
{
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(0,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFDataRef xml;unsigned int i;if(!d)return;
    for(i=0;names[i];++i){CFStringRef key=CFStringCreateWithCString(0,names[i],kCFStringEncodingASCII);
        CFTypeRef value=key?IORegistryEntryCreateCFProperty(s,key,0,0):0;
        if(value){CFDictionarySetValue(d,key,value);CFRelease(value);}if(key)CFRelease(key);}
    xml=CFPropertyListCreateXMLData(0,d);CFRelease(d);
    if(xml){fwrite(CFDataGetBytePtr(xml),1,CFDataGetLength(xml),stdout);CFRelease(xml);}
}
int main(int argc,char **argv)
{
    io_service_t s;io_connect_t c;unsigned char op,out[56];IOByteCount n;unsigned int i;
    const char *usb[]={"idVendor","idProduct","bcdDevice","USB Product Name","USB Vendor Name",0};
    const char *core[]={"CFBundleIdentifier","MACAddress","USBLinkSpeedMbps","USBBulkMaxPacket","RadioRunning",
        "FirmwareRunning","RFChannelAtCalibration","NetworkSessionEnabled","RXLastError","RXUSBSubmitted","RXUSBCompleted",
        "RXUSBErrors","RXUSBTimeouts","RXWatchdogs","RXBeacons","RXMalformed","RXFCSErrors","RXICVErrors",
        "AssociationTXLastError","AssociationTXSubmitted","AssociationTXCompleted","AssociationTXErrors","NetworkSessionActive","WPA2DataPortAuthorized",
        "PassiveScanActive","PassiveScanValid","PassiveScanFailed","PassiveScanBSSCount",0};
    if(argc==2&&!strcmp(argv[1],"--usb-present")){
        s=device();if(!s)return 2;IOObjectRelease(s);
        s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDriver"));
        if(!s)s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortPCI"));
        if(s){IOObjectRelease(s);fputs("Apple AirPort also present: activation refused.\n",stderr);return 1;}
        return 0;
    }
    if(argc!=1)return 1;
    s=device();puts(s?"USB DWA-121 A1 present":"USB DWA-121 A1 absent");
    if(s){properties(s,usb);IOObjectRelease(s);}
    s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("DWA121Diagnostic"));
    if(s){properties(s,core);IOObjectRelease(s);}else puts("Project driver not attached");
    s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("DWA121NetworkInterface"));
    if(s){io_iterator_t children=0;io_service_t child;const char *bsd[]={"BSD Name",0};
        if(!IORegistryEntryGetChildIterator(s,kIOServicePlane,&children)){
            while((child=IOIteratorNext(children))){if(IOObjectConformsTo(child,"IOEthernetInterface"))properties(child,bsd);IOObjectRelease(child);}
            IOObjectRelease(children);}
        IOObjectRelease(s);}
    s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));
    if(!s){puts("AirPort companion not attached");return 0;}
    if(IOServiceOpen(s,mach_task_self(),12,&c)){IOObjectRelease(s);return 1;}IOObjectRelease(s);
    op=13;n=0;printf("Radio result: %08lx\n",(unsigned long)IOConnectMethodStructureIStructureO(c,0,1,&n,&op,out));
    op=18;n=56;
    if(!IOConnectMethodStructureIStructureO(c,0,1,&n,&op,out)&&n==56){
        printf("Association port=%u result=%u SSID=",((unsigned int)out[2]<<8)|out[3],out[55]);
        for(i=6;i<38&&out[i];++i)putchar(out[i]>=32&&out[i]<=126?out[i]:'?');
        printf(" BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n",out[46],out[47],out[48],out[49],out[50],out[51]);
    }else puts("Association unavailable");
    IOServiceClose(c);return 0;
}
