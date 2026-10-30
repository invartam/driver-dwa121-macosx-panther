/* Panther-only, opt-in AirPort status and passive scan companion.
 * Wire evidence: docs/airport-p02-architecture.md. No Apple code included. */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <kern/clock.h>
#include "../net80211/scan.h"
extern "C" {
#include <sys/proc.h>
#include <sys/vm.h>
}

class AirPortDWA121 : public IOService {
    OSDeclareDefaultStructors(AirPortDWA121)
    IOService *hardware,*controller;
    OSObject *originalClass;
    IOLock *lock;
    void detachHardware();
public:
    virtual bool init(OSDictionary * = 0);
    virtual void free();
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **);
    IOReturn scan(bool,rtl_u32 *,rtl_bss_table *);
    IOReturn readPower();
    IOReturn readStatus(void *,IOByteCount *);
};
/* Deliberately does not start with AirPort: Apple's iterator matches prefixes. */
class DWA121AirPortClient : public IOUserClient {
    OSDeclareDefaultStructors(DWA121AirPortClient)
    AirPortDWA121 *radio;
    IOExternalMethod method;
    IOLock *scanLock;
    rtl_bss_table *snapshot;
    rtl_u32 generation,snapshotTime;
    unsigned char requested[33];
    unsigned int requestedLength;
    bool snapshotReady;
    IOReturn scanCommand(unsigned int,void *,void *);
    IOReturn command(void *,void *,IOByteCount,IOByteCount *);
public:
    virtual bool initWithTask(task_t,void *,UInt32);
    virtual bool start(IOService *);
    virtual void free();
    virtual IOReturn clientClose();
    virtual IOReturn clientDied();
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **,UInt32);
};
OSDefineMetaClassAndStructors(AirPortDWA121,IOService)
OSDefineMetaClassAndStructors(DWA121AirPortClient,IOUserClient)
bool AirPortDWA121::init(OSDictionary *p)
{
    hardware=controller=0;originalClass=0;lock=0;
    if(!IOService::init(p))return false;
    lock=IOLockAlloc();return lock!=0;
}
void AirPortDWA121::free()
{ detachHardware();if(lock)IOLockFree(lock);IOService::free(); }
bool AirPortDWA121::start(IOService *provider)
{
    if(!provider||!provider->metaCast("DWA121NetworkInterface"))return false;
    IOService *usb=provider->getProvider();
    if(!usb||!usb->metaCast("DWA121Diagnostic")||!IOService::start(provider))return false;
    originalClass=provider->copyProperty("IOClass");
    if(!OSDynamicCast(OSString,originalClass))return false;
    hardware=usb;hardware->retain();controller=provider;controller->retain();
    /* NetworkConfig::PortScanner inspects the controller's IOClass property,
     * not its C++ metaclass. Publish our actual compatibility facade's name.
     * The original controller and all packet methods remain untouched. */
    if(!controller->setProperty("IOClass","AirPortDWA121"))return false;
    setProperty("P02ReadOnly",true);
    setProperty("P031RSSIAvailable",false);
    setProperty("P02Protocol","Panther status 13,18 / passive scan 8,9");
    registerService();return true;
}
void AirPortDWA121::detachHardware()
{
    if(!lock)return;
    IOLockLock(lock);
    IOService *oldController=controller,*oldHardware=hardware;
    OSObject *oldClass=originalClass;
    OSObject *value=controller?controller->copyProperty("IOClass"):0;
    OSString *name=OSDynamicCast(OSString,value);
    if(name&&name->isEqualTo("AirPortDWA121")&&oldClass)
        controller->setProperty("IOClass",oldClass);
    if(value)value->release();
    hardware=controller=0;originalClass=0;
    IOLockUnlock(lock);
    /* UI clients may outlive USB termination. Never pin the USB kext until
     * those stale handles close; calls now fail without touching providers. */
    if(oldClass)oldClass->release();
    if(oldController)oldController->release();
    if(oldHardware)oldHardware->release();
}
void AirPortDWA121::stop(IOService *provider)
{ detachHardware();IOService::stop(provider); }
IOReturn AirPortDWA121::readPower()
{
    IOLockLock(lock);
    OSObject *value=(!isInactive()&&hardware&&!hardware->isInactive())?
        hardware->copyProperty("RadioRunning"):0;
    IOLockUnlock(lock);
    if(!value)return kIOReturnNotAttached;
    bool on=value==kOSBooleanTrue;value->release();
    return on?3:0;
}
/* AppleAirPort::getWirelessStatus initializes the first word to zero,
 * returns port status zero when HCF status is unavailable, and writes the
 * hardware address at +40. WirelessGetInfo2 copies it to +58 and then reads
 * power independently with command 13. Other information is unavailable in
 * P0.2, not a claim of association or a synthetic signal measurement. */
IOReturn AirPortDWA121::readStatus(void *out,IOByteCount *size)
{
    if(!out||!size||*size!=56)return kIOReturnBadArgument;
    IOLockLock(lock);
    OSObject *value=(!isInactive()&&hardware&&!hardware->isInactive())?
        hardware->copyProperty("MACAddress"):0;
    IOLockUnlock(lock);
    if(!value)return kIOReturnNotAttached;
    OSData *mac=OSDynamicCast(OSData,value);
    if(!mac||mac->getLength()!=6){value->release();return kIOReturnNotReady;}
    bzero(out,56);
    /* Native Card Mode=1: infrastructure. AppleAirPort derives this from
     * CREATE_IBSS (HCF 0xfc81) being zero. RTL station mode is infrastructure. */
    ((unsigned char *)out)[5]=1;
    bcopy(mac->getBytesNoCopy(),(unsigned char *)out+40,6);
    value->release();*size=56;
    IOLog("DWA121 AirPort P02: command=18 status=unknown MAC=provider result=success\n");
    return kIOReturnSuccess;
}
IOReturn AirPortDWA121::newUserClient(task_t task,void *security,UInt32 type,IOUserClient **out)
{
    if(!out)return kIOReturnBadArgument;
    *out=0;
    if(type!=12)return kIOReturnUnsupported;
    if(isInactive())return kIOReturnNotAttached;
    DWA121AirPortClient *c=new DWA121AirPortClient;
    if(!c)return kIOReturnNoMemory;
    if(!c->initWithTask(task,security,type)){c->release();return kIOReturnNoMemory;}
    if(!c->attach(this)){c->release();return kIOReturnNotAttached;}
    if(!c->start(this)){c->detach(this);c->release();return kIOReturnNotReady;}
    *out=c;return kIOReturnSuccess;
}
bool DWA121AirPortClient::initWithTask(task_t task,void *security,UInt32 type)
{
    radio=0;scanLock=0;snapshot=0;generation=snapshotTime=0;snapshotReady=false;
    requestedLength=0;bzero(requested,sizeof(requested));bzero(&method,sizeof(method));
    if(type!=12||!IOUserClient::initWithTask(task,security,type))return false;
    method.object=this;method.func=(IOMethod)&DWA121AirPortClient::command;
    method.flags=kIOUCStructIStructO;method.count0=0xffffffffU;
    /* Nonzero sentinel preserves four-argument dispatch for zero-length output.
     * AppleAirPort::start uses 0xffffffff at offsets 0x90/0x94. */
    method.count1=0xffffffffU;
    scanLock=IOLockAlloc();snapshot=(rtl_bss_table *)IOMalloc(sizeof(*snapshot));
    if(snapshot)bzero(snapshot,sizeof(*snapshot));
    return scanLock&&snapshot;
}
bool DWA121AirPortClient::start(IOService *p)
{
    AirPortDWA121 *r=OSDynamicCast(AirPortDWA121,p);
    if(!r||!IOUserClient::start(p))return false;
    radio=r;radio->retain();return true;
}
void DWA121AirPortClient::free()
{
    if(snapshot)IOFree(snapshot,sizeof(*snapshot));
    if(scanLock)IOLockFree(scanLock);
    if(radio){radio->release();radio=0;}IOUserClient::free();
}
IOReturn DWA121AirPortClient::clientClose()
{ terminate();return kIOReturnSuccess; }
IOReturn DWA121AirPortClient::clientDied() { return clientClose(); }
IOExternalMethod *DWA121AirPortClient::getTargetAndMethodForIndex(IOService **target,UInt32 index)
{ if(!target||index!=0)return 0;*target=this;return &method; }
IOReturn DWA121AirPortClient::command(void *in,void *out,IOByteCount n,IOByteCount *outSize)
{
    if(!in||!outSize||n<1)return kIOReturnBadArgument;
    struct proc *caller=current_proc();
    if(*(unsigned char *)in!=9&&*(unsigned char *)in!=13)
    IOLog("DWA121 AirPort P02: caller=%s pid=%d op=%u in=%lu out=%lu\n",
        caller?caller->p_comm:"?",caller?caller->p_pid:-1,
        (unsigned int)*(unsigned char *)in,(unsigned long)n,(unsigned long)*outSize);
    if(isInactive()||!radio)return kIOReturnNotAttached;
    unsigned int op=*(unsigned char *)in;
    if(op==8||op==9){
        if((op==8&&(n!=34||*outSize!=0))||
           (op==9&&(n!=5||*outSize!=50||!out)))return kIOReturnBadArgument;
        IOLockLock(scanLock);
        IOReturn r=scanCommand(op,(unsigned char *)in+1,out);
        IOLockUnlock(scanLock);return r;
    }
    if(*(unsigned char *)in==18){
        if(n!=1)return kIOReturnBadArgument;
        return radio->readStatus(out,outSize);
    }
    if(*(unsigned char *)in!=13)return kIOReturnUnsupported;
    if(n!=1||*outSize!=0)return kIOReturnBadArgument;
    return radio->readPower();
}

IOReturn AirPortDWA121::scan(bool start,rtl_u32 *generation,rtl_bss_table *table)
{
    IOLockLock(lock);
    IOReturn r=kIOReturnNotAttached;
    if(!isInactive()&&hardware&&!hardware->isInactive())
        r=hardware->callPlatformFunction(start?DWA_SCAN_START:DWA_SCAN_COPY,
                                         false,generation,table,0,0);
    IOLockUnlock(lock);return r;
}
static rtl_u32 nativeScanNow()
{
    AbsoluteTime ticks;UInt64 ns;
    clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);
    return (rtl_u32)(ns/1000000ULL);
}
IOReturn DWA121AirPortClient::scanCommand(unsigned int op,void *in,void *out)
{
    const unsigned char *p=(const unsigned char *)in;
    if(op==8){
        snapshotReady=false;generation=0;bzero(snapshot,sizeof(*snapshot));
        if(p[32])return kIOReturnBadArgument;
        bcopy(p,requested,33);requestedLength=0;
        while(requestedLength<32&&requested[requestedLength])++requestedLength;
        for(unsigned int i=requestedLength;i<33;++i)
            if(requested[i])return kIOReturnBadArgument;
        IOReturn r=radio->scan(true,&generation,0);
        IOLog("DWA121 AirPort scan: start result=%08lx generation=%lu\n",
            (unsigned long)r,(unsigned long)generation);
        return r;
    }
    if(!generation)return (IOReturn)0x20000109;
    /* Even a retained client snapshot must disappear when the radio stops. */
    if(radio->readPower()!=3){snapshotReady=false;generation=0;return (IOReturn)0x20000109;}
    if(!snapshotReady){
        IOReturn r=radio->scan(false,&generation,snapshot);
        if(r==kIOReturnBusy)return (IOReturn)0x20000108;
        if(r!=kIOReturnSuccess){
            /* Panther advances its index on arbitrary errors. Always end a
             * cancelled/invalidated scan, rather than an unbounded error loop. */
            generation=0;return (IOReturn)0x20000109;
        }
        snapshotTime=nativeScanNow();
        rtl_u32 now=snapshotTime;
        for(unsigned int i=0;i<snapshot->count;++i)
            if((rtl_u32)(now-snapshot->entries[i].last_seen_ms)>(rtl_u32)(now-snapshotTime))
                snapshotTime=snapshot->entries[i].last_seen_ms;
        snapshotReady=true;
    }
    if((rtl_u32)(nativeScanNow()-snapshotTime)>=DWA_SCAN_TTL_MS)return (IOReturn)0x20000109;
    rtl_u32 index=((rtl_u32)p[0]<<24)|((rtl_u32)p[1]<<16)|((rtl_u32)p[2]<<8)|p[3];
    unsigned int n=0;
    unsigned char record[50];
    for(unsigned int i=0;i<snapshot->count;++i){
        const rtl_bss *b=&snapshot->entries[i];
        if(requestedLength&&(b->ssid_length!=requestedLength||
            bcmp(b->ssid,requested,requestedLength)))continue;
        if(!dwa_scan_native(b,record))continue;
        if(n++==index){bcopy(record,out,50);return kIOReturnSuccess;}
    }
    IOLog("DWA121 AirPort scan: end generation=%lu native=%u BSSID=%u\n",
        (unsigned long)generation,n,snapshot->count);
    return (IOReturn)0x20000109;
}
