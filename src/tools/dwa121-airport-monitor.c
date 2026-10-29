/* Panther's Apple80211Monitor matches only Apple's exact driver classes.
 * Publish the verified native state/notifications for our compatibility facade.
 * Foreground process; state key is session-owned and removed even on death.
 * No keys, scan results, link association or RSSI are manufactured. */
#include <SystemConfiguration/SystemConfiguration.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig;stopping=1; }
static void notify(CFStringRef event,CFStringRef value)
{
    const void *keys[]={event},*values[]={value};
    CFDictionaryRef d=CFDictionaryCreate(0,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!d)return;
    CFNotificationCenterPostNotificationWithOptions(CFNotificationCenterGetDistributedCenter(),
        CFSTR("AirPort_Notification"),CFSTR("AirPort Monitor"),d,2);
    CFRelease(d);
}
static int appleCard(void)
{
    io_service_t s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDriver"));
    if(!s)s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortPCI"));
    if(s){IOObjectRelease(s);return 1;}return 0;
}
static CFStringRef snapshot(int *power)
{
    io_service_t s;io_registry_entry_t controller=0,child;io_iterator_t children=0;
    io_connect_t c=0;CFTypeRef name=0;IOReturn r;unsigned char cmd=13,out=0;IOByteCount n=0;
    s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));
    if(!s)return 0;
    r=IOServiceOpen(s,mach_task_self(),12,&c);
    if(!r)r=IOConnectMethodStructureIStructureO(c,0,1,&n,&cmd,&out);
    if(c)IOServiceClose(c);
    if(r!=0&&r!=3){IOObjectRelease(s);return 0;}
    *power=r==3;
    if(!IORegistryEntryGetParentEntry(s,kIOServicePlane,&controller)){
        if(!IORegistryEntryGetChildIterator(controller,kIOServicePlane,&children)){
            while((child=IOIteratorNext(children))){
                if(!name&&IOObjectConformsTo(child,"IOEthernetInterface"))
                    name=IORegistryEntryCreateCFProperty(child,CFSTR("BSD Name"),0,0);
                IOObjectRelease(child);
            }
            IOObjectRelease(children);
        }
        IOObjectRelease(controller);
    }
    IOObjectRelease(s);
    if(name&&CFGetTypeID(name)!=CFStringGetTypeID()){CFRelease(name);name=0;}
    return (CFStringRef)name;
}
static CFDictionaryRef state(int power)
{
    /* Same types as Apple80211Monitor::updateSC. Link Status=0 means that
     * association status is not implemented by the P0.2 compatibility API. */
    short p=power,mode=1,unknown=0;CFDictionaryRef d=0;
    CFNumberRef v[3];const void *keys[]={CFSTR("Power Status"),CFSTR("Card Mode"),CFSTR("Link Status")};
    v[0]=CFNumberCreate(0,kCFNumberShortType,&p);
    v[1]=CFNumberCreate(0,kCFNumberShortType,&mode);
    v[2]=CFNumberCreate(0,kCFNumberShortType,&unknown);
    if(v[0]&&v[1]&&v[2])d=CFDictionaryCreate(0,keys,(const void **)v,3,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(v[0])CFRelease(v[0]);if(v[1])CFRelease(v[1]);if(v[2])CFRelease(v[2]);return d;
}

int main(int argc,char **argv)
{
    SCDynamicStoreRef store;CFStringRef owned=0;int old=-1,seen=0,result=0;
    struct sigaction sa;
    if(argc!=2||strcmp(argv[1],"--watch")||geteuid()!=0){fprintf(stderr,"Usage (root): dwa121-airport-monitor --watch\n");return 1;}
    if(appleCard()){fprintf(stderr,"Apple AirPort also present; global legacy notifications are ambiguous.\n");return 1;}
    memset(&sa,0,sizeof(sa));sa.sa_handler=stop;sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,&sa,0);sigaction(SIGINT,&sa,0);sigaction(SIGHUP,&sa,0);
    store=SCDynamicStoreCreate(0,CFSTR("DWA121 P0.2 state relay"),0,0);if(!store)return 1;
    while(!stopping){
        int power=0;CFStringRef name,key=0;CFDictionaryRef d;
        if(appleCard()){result=1;break;}
        name=snapshot(&power);
        if(name){key=SCDynamicStoreKeyCreateNetworkInterfaceEntity(0,kSCDynamicStoreDomainState,name,kSCEntNetAirPort);CFRelease(name);}
        if(owned&&(!key||!CFEqual(owned,key))){SCDynamicStoreRemoveValue(store,owned);CFRelease(owned);owned=0;old=-1;}
        if(!key){if(seen){notify(CFSTR("AirPortDriverLoaded"),CFSTR("0"));seen=0;}}
        else {
            if(!owned||power!=old){
                d=state(power);if(!d){CFRelease(key);result=1;break;}
                if(!(owned?SCDynamicStoreSetValue(store,key,d):SCDynamicStoreAddTemporaryValue(store,key,d))){
                    CFRelease(d);CFRelease(key);fprintf(stderr,"Cannot own/update AirPort state key; existing state preserved.\n");result=1;break;
                }
                CFRelease(d);if(!owned)owned=CFRetain(key);
                printf("DWA121 state: Power Status=%d\n",power);fflush(stdout);old=power;
                if(power)notify(CFSTR("Power_On"),CFSTR("1"));
            }
            if(!seen){notify(CFSTR("AirPortDriverLoaded"),CFSTR("1"));seen=1;}
            CFRelease(key);
        }
        sleep(1);
    }
    if(owned){SCDynamicStoreRemoveValue(store,owned);CFRelease(owned);}
    CFRelease(store);return result;
}
