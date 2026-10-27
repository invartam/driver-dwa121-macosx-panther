/* Panther-native checker of the optional, deliberately offline BSD attachment.
 * --deny-up exercises the real SIOCSIFFLAGS/driver enable path as root. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static CFTypeRef property(io_registry_entry_t entry,const char *name)
{
    CFStringRef key=CFStringCreateWithCString(0,name,kCFStringEncodingASCII);
    CFTypeRef value=IORegistryEntryCreateCFProperty(entry,key,0,0);
    CFRelease(key);return value;
}
static int boolean(io_registry_entry_t entry,const char *key,int expected)
{
    CFTypeRef value=property(entry,key);
    int good=value && CFGetTypeID(value)==CFBooleanGetTypeID() &&
        CFBooleanGetValue((CFBooleanRef)value)==expected;
    if(value)CFRelease(value);
    if(!good)fprintf(stderr,"FAIL: %s\n",key);
    return good;
}
static long number(io_registry_entry_t entry,const char *key)
{
    CFTypeRef value=property(entry,key);long n=-1;
    if(value && CFGetTypeID(value)==CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)value,kCFNumberLongType,&n);
    if(value)CFRelease(value);return n;
}
int main(int argc,char **argv)
{
    io_iterator_t it=0,children=0;io_service_t service=0,extra=0,child=0,parent=0;
    CFTypeRef a=0,b=0,name=0;char bsd[IFNAMSIZ];struct ifreq req;
    int deny=0,absent=0,fd=-1,result=1,flags,rc,saved_errno,running;long before,after;
    unsigned int i;
    memset(bsd,0,sizeof(bsd));
    if(argc==2 && !strcmp(argv[1],"--deny-up"))deny=1;
    else if(argc==2 && !strcmp(argv[1],"--absent"))absent=1;
    else if(argc!=1){fprintf(stderr,"Usage: dwa121-netcheck [--deny-up|--absent]\n");return 1;}
    if(deny && geteuid()!=0){fprintf(stderr,"--deny-up requires root\n");return 1;}
    if(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("DWA121Ethernet"),&it)!=KERN_SUCCESS)goto done;
    service=IOIteratorNext(it);extra=IOIteratorNext(it);
    if(absent){result=service?1:0;goto done;}
    if(!service || extra){fprintf(stderr,"Expected exactly one DWA121Ethernet service\n");goto done;}
    if(!boolean(service,"ExperimentalOfflineOnly",1) ||
       !boolean(service,"EthernetDataPathReady",0) ||
       !boolean(service,"EthernetPortAuthorized",0) ||
       number(service,"IOLinkStatus")!=1 || number(service,"IOLinkSpeed")!=0)goto done;
    if(IORegistryEntryGetParentEntry(service,kIOServicePlane,&parent)!=KERN_SUCCESS ||
       !IOObjectConformsTo(parent,"DWA121Diagnostic"))goto done;
    a=property(service,"IOMACAddress");b=property(parent,"MACAddress");
    if(!a || !b || CFGetTypeID(a)!=CFDataGetTypeID() || CFGetTypeID(b)!=CFDataGetTypeID() ||
       CFDataGetLength((CFDataRef)a)!=6 || !CFEqual(a,b))goto done;
    if(IORegistryEntryGetChildIterator(service,kIOServicePlane,&children)!=KERN_SUCCESS)goto done;
    while((child=IOIteratorNext(children))) {
        if(IOObjectConformsTo(child,"IOEthernetInterface"))break;
        IOObjectRelease(child);child=0;
    }
    if(!child)goto done;
    name=property(child,"BSD Name");
    if(!name || CFGetTypeID(name)!=CFStringGetTypeID() ||
       !CFStringGetCString((CFStringRef)name,bsd,sizeof(bsd),kCFStringEncodingASCII) ||
       bsd[0]!='e' || bsd[1]!='n' || !bsd[2])goto done;
    for(i=2;bsd[i];++i)if(bsd[i]<'0'||bsd[i]>'9')goto done;
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)goto done;
    memset(&req,0,sizeof(req));strcpy(req.ifr_name,bsd);
    if(ioctl(fd,SIOCGIFFLAGS,&req)<0)goto done;
    flags=req.ifr_flags;
    if(flags&IFF_RUNNING){fprintf(stderr,"FAIL: interface already running\n");goto done;}
    if(ioctl(fd,SIOCGIFMTU,&req)<0 || req.ifr_mtu!=1500)goto done;
    printf("Interface: %s\nMAC matches USB device; MTU=1500; link=inactive; speed=0\n",bsd);
    if(deny) {
        before=number(service,"EthernetEnableRefusals");if(before<0)goto done;
        req.ifr_flags=flags|IFF_UP;errno=0;
        rc=ioctl(fd,SIOCSIFFLAGS,&req);saved_errno=errno;
        after=number(service,"EthernetEnableRefusals");
        running=ioctl(fd,SIOCGIFFLAGS,&req)<0 || (req.ifr_flags&IFF_RUNNING);
        printf("Activation request: ioctl=%d errno=%d flags_before=0x%x flags_after=0x%x refusals=%ld->%ld\n",
            rc,saved_errno,flags&0xffff,req.ifr_flags&0xffff,before,after);
        /* Panther may leave administrative IFF_UP set when enable fails.
         * Restore our original flags even after the expected rejection. */
        req.ifr_flags=flags;
        if(ioctl(fd,SIOCSIFFLAGS,&req)<0 && !(flags&IFF_UP))goto done;
        /* xnu-517.12.7 ifioctl(SIOCSIFFLAGS) discards dlil_ioctl's error at
         * the final return(0). Do not treat rc==0 as hardware activation.
         * Require proof of a new driver refusal and absence of RUNNING. */
        if(after<=before || running || ioctl(fd,SIOCGIFFLAGS,&req)<0 ||
           (req.ifr_flags&IFF_RUNNING) || (req.ifr_flags&IFF_UP)!=(flags&IFF_UP) ||
           number(service,"IOLinkStatus")!=1)goto done;
        printf("Activation refused by driver: refusals=%ld->%ld; RUNNING=0; original flags restored\n",before,after);
    }
    result=0;
done:
    if(fd>=0)close(fd);
    if(a)CFRelease(a);if(b)CFRelease(b);if(name)CFRelease(name);
    if(child)IOObjectRelease(child);if(parent)IOObjectRelease(parent);
    if(children)IOObjectRelease(children);
    if(extra)IOObjectRelease(extra);if(service)IOObjectRelease(service);if(it)IOObjectRelease(it);
    printf("%s: Ethernet %s\n",result?"FAIL":"PASS",absent?"absent":(deny?"offline activation guard":"offline attachment"));
    return result;
}
