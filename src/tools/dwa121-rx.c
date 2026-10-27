/* Administrator-only RX stop command; the kernel independently checks root. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    io_iterator_t iterator=0;io_service_t service;
    kern_return_t result;int found=0,failed=0;
    CFMutableDictionaryRef command;
    if(argc!=2||strcmp(argv[1],"stop")){
        fprintf(stderr,"Usage: sudo dwa121-rx stop\n");return 1;
    }
    command=CFDictionaryCreateMutable(kCFAllocatorDefault,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!command)return 1;
    CFDictionarySetValue(command,CFSTR("RXStop"),kCFBooleanTrue);
    result=IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("DWA121Diagnostic"),&iterator);
    if(result!=KERN_SUCCESS){CFRelease(command);return 1;}
    while((service=IOIteratorNext(iterator))){
        found=1;result=IORegistryEntrySetCFProperties(service,command);
        IOObjectRelease(service);
        if(result!=KERN_SUCCESS){fprintf(stderr,"RX stop failed: 0x%08lx\n",(unsigned long)result);failed=1;}
    }
    IOObjectRelease(iterator);CFRelease(command);
    if(!found){fprintf(stderr,"No DWA121Diagnostic service\n");return 2;}
    return failed?1:0;
}
