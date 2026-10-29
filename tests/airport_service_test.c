/* Regression: an empty SCPreferences AirPort entity is not published by
 * Panther configd, so ClientController forces the native menu Off.
 * Test the applied Dynamic Store, not only the preferences file. */
#include <SystemConfiguration/SystemConfiguration.h>
#include <stdio.h>
#include <string.h>
static int owned(CFPropertyListRef value)
{
    CFTypeRef marker;
    if(!value||CFGetTypeID(value)!=CFDictionaryGetTypeID())return 0;
    marker=CFDictionaryGetValue(value,CFSTR("DWA121P02Owner"));
    return marker&&CFEqual(marker,CFSTR("local.airportp02.DWA121"));
}
int main(int argc,char **argv)
{
    SCDynamicStoreRef store;CFPropertyListRef air,interface;int ok,present;
    if(argc!=2||(strcmp(argv[1],"present")&&strcmp(argv[1],"absent")))return 1;
    present=!strcmp(argv[1],"present");
    store=SCDynamicStoreCreate(0,CFSTR("DWA121 entity regression"),0,0);
    if(!store)return 1;
    air=SCDynamicStoreCopyValue(store,CFSTR("Setup:/Network/Service/DWA121-P02/AirPort"));
    interface=SCDynamicStoreCopyValue(store,CFSTR("Setup:/Network/Service/DWA121-P02/Interface"));
    if(present){
        CFTypeRef hardware=0;
        if(interface&&CFGetTypeID(interface)==CFDictionaryGetTypeID())
            hardware=CFDictionaryGetValue(interface,CFSTR("Hardware"));
        ok=owned(air)&&hardware&&CFEqual(hardware,CFSTR("AirPort"));
    }else ok=!air&&!interface;
    if(air)CFRelease(air);if(interface)CFRelease(interface);CFRelease(store);
    printf("%s: applied AirPort service %s\n",ok?"PASS":"FAIL",present?"present":"absent");
    return ok?0:1;
}
