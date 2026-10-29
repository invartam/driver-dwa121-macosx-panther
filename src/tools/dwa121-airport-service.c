/* Panther SCPreferences adapter. Adds/removes only an owned P0.2 service.
 * No IPv4 configuration, password, existing service, or existing service priority is changed (own ID appended last). */
#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCPreferencesPath.h>
#include <IOKit/IOKitLib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#define SERVICE CFSTR("/NetworkServices/DWA121-P02")
#define OWNER CFSTR("DWA121P02Owner")
#define OWNER_VALUE CFSTR("local.airportp02.DWA121")
static CFMutableDictionaryRef dictionary(void)
{ return CFDictionaryCreateMutable(0,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks); }
static CFStringRef interfaceName(void)
{
    io_service_t controller;io_registry_entry_t child;io_iterator_t children=0;
    CFTypeRef name=0;int facade=0;
    controller=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("DWA121NetworkInterface"));
    if(!controller)return 0;
    if(!IORegistryEntryGetChildIterator(controller,kIOServicePlane,&children)) {
        while((child=IOIteratorNext(children))){
            if(IOObjectConformsTo(child,"AirPortDWA121"))facade=1;
            if(!name&&IOObjectConformsTo(child,"IOEthernetInterface"))
                name=IORegistryEntryCreateCFProperty(child,CFSTR("BSD Name"),0,0);
            IOObjectRelease(child);
        }
        IOObjectRelease(children);
    }
    IOObjectRelease(controller);
    if(name&&(!facade||CFGetTypeID(name)!=CFStringGetTypeID())){CFRelease(name);name=0;}
    return (CFStringRef)name;
}

static int updateOrder(SCPreferencesRef prefs,CFStringRef set,int remove)
{
    CFStringRef path=CFStringCreateWithFormat(0,0,CFSTR("%@/Network/Global/IPv4"),set);
    CFDictionaryRef old=path?SCPreferencesPathGetValue(prefs,path):0;
    CFArrayRef list=old?CFDictionaryGetValue(old,CFSTR("ServiceOrder")):0;
    CFMutableDictionaryRef value=0;CFMutableArrayRef order=0;CFIndex i;int ok=0;
    if(!list||CFGetTypeID(list)!=CFArrayGetTypeID())goto done;
    value=CFDictionaryCreateMutableCopy(0,0,old);order=CFArrayCreateMutableCopy(0,0,list);
    if(!value||!order)goto done;
    for(i=CFArrayGetCount(order);i>0;--i){
        if(CFEqual(CFArrayGetValueAtIndex(order,i-1),CFSTR("DWA121-P02"))){
            if(!remove)goto done;
            CFArrayRemoveValueAtIndex(order,i-1);
        }
    }
    if(!remove)CFArrayAppendValue(order,CFSTR("DWA121-P02"));
    CFDictionarySetValue(value,CFSTR("ServiceOrder"),order);
    ok=SCPreferencesPathSetValue(prefs,path,value);
done:
    if(order)CFRelease(order);if(value)CFRelease(value);if(path)CFRelease(path);return ok;
}
int main(int argc,char **argv)
{
    SCPreferencesRef prefs;CFDictionaryRef old;CFStringRef set=0,link=0,name=0;
    CFMutableDictionaryRef service=0,interface=0,airport=0;int ok=0,remove;
    if(argc!=2||(strcmp(argv[1],"add")&&strcmp(argv[1],"remove"))||geteuid()!=0){
        fprintf(stderr,"Usage (root): dwa121-airport-service add|remove\n");return 1;
    }
    remove=!strcmp(argv[1],"remove");
    prefs=SCPreferencesCreate(0,CFSTR("DWA121 P0.2"),0);
    if(!prefs||!SCPreferencesLock(prefs,FALSE)){if(prefs)CFRelease(prefs);return 1;}
    old=SCPreferencesPathGetValue(prefs,SERVICE);
    if(remove){
        CFTypeRef marker=old?CFDictionaryGetValue(old,OWNER):0;
        if(!marker||!CFEqual(marker,OWNER_VALUE)){fprintf(stderr,"No owned P0.2 service; unchanged.\n");goto done;}
        set=CFDictionaryGetValue(old,CFSTR("DWA121P02Set"));
        if(!set||CFGetTypeID(set)!=CFStringGetTypeID()){set=0;goto done;}
        CFRetain(set);
        link=CFStringCreateWithFormat(0,0,CFSTR("%@/Network/Service/DWA121-P02"),set);
        if(!link)goto done;
        if(SCPreferencesPathGetLink(prefs,link)) {
            if(!CFEqual(SCPreferencesPathGetLink(prefs,link),SERVICE))goto done;
            if(!SCPreferencesPathRemoveValue(prefs,link))goto done;
        }
        if(!updateOrder(prefs,set,1)||!SCPreferencesPathRemoveValue(prefs,SERVICE))goto done;
    }else{
        if(old){fprintf(stderr,"Service path already exists; unchanged.\n");goto done;}
        name=interfaceName();if(!name){fprintf(stderr,"No DWA121 BSD interface.\n");goto done;}
        set=SCPreferencesGetValue(prefs,CFSTR("CurrentSet"));
        if(!set||CFGetTypeID(set)!=CFStringGetTypeID()){set=0;goto done;}
        CFRetain(set);
        link=CFStringCreateWithFormat(0,0,CFSTR("%@/Network/Service/DWA121-P02"),set);
        if(!link||SCPreferencesPathGetValue(prefs,link))goto done;
        service=dictionary();interface=dictionary();airport=dictionary();
        if(!service||!interface||!airport)goto done;
        CFDictionarySetValue(interface,CFSTR("Hardware"),CFSTR("AirPort"));
        CFDictionarySetValue(interface,CFSTR("Type"),CFSTR("Ethernet"));
        CFDictionarySetValue(interface,CFSTR("DeviceName"),name);
        CFDictionarySetValue(service,CFSTR("Interface"),interface);
        CFDictionarySetValue(service,CFSTR("AirPort"),airport);
        CFDictionarySetValue(service,CFSTR("UserDefinedName"),CFSTR("DWA-121 AirPort P0.2"));
        CFDictionarySetValue(service,OWNER,OWNER_VALUE);
        CFDictionarySetValue(service,CFSTR("DWA121P02Set"),set);
        if(!updateOrder(prefs,set,0)||!SCPreferencesPathSetValue(prefs,SERVICE,service)||!SCPreferencesPathSetLink(prefs,link,SERVICE))goto done;
    }
    if(!SCPreferencesCommitChanges(prefs))goto done;
    if(!SCPreferencesApplyChanges(prefs)){
        fprintf(stderr,"Preferences committed; apply failed. Inspect or remove P0.2 service.\n");goto done;
    }
    ok=1;puts(remove?"P0.2 service removed.":"P0.2 AirPort service registered (status only, no IP changes).");
done:
    if(airport)CFRelease(airport);if(interface)CFRelease(interface);if(service)CFRelease(service);
    if(name)CFRelease(name);if(link)CFRelease(link);if(set)CFRelease(set);
    SCPreferencesUnlock(prefs);CFRelease(prefs);return ok?0:1;
}
