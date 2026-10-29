/* Panther-only, opt-in read-only AirPort control companion.
 * Wire evidence: docs/airport-p02-architecture.md. No Apple code included. */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
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
    IOReturn readPower();
    IOReturn readStatus(void *,IOByteCount *);
};
/* Deliberately does not start with AirPort: Apple's iterator matches prefixes. */
class DWA121AirPortClient : public IOUserClient {
    OSDeclareDefaultStructors(DWA121AirPortClient)
    AirPortDWA121 *radio;
    IOExternalMethod method;
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
    setProperty("P02Protocol","Panther radio status / commands 13,18");
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
    IOLog("DWA121 AirPort P02: command=13 RadioRunning=%u (native return bits)\n",on?1:0);
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
    IOLog("DWA121 AirPort P02: opened type=12 (read-only)\n");
    *out=c;return kIOReturnSuccess;
}
bool DWA121AirPortClient::initWithTask(task_t task,void *security,UInt32 type)
{
    radio=0;bzero(&method,sizeof(method));
    if(type!=12||!IOUserClient::initWithTask(task,security,type))return false;
    method.object=this;method.func=(IOMethod)&DWA121AirPortClient::command;
    method.flags=kIOUCStructIStructO;method.count0=0xffffffffU;
    /* Nonzero sentinel preserves four-argument dispatch for zero-length output.
     * AppleAirPort::start uses 0xffffffff at offsets 0x90/0x94. */
    method.count1=0xffffffffU;return true;
}
bool DWA121AirPortClient::start(IOService *p)
{
    AirPortDWA121 *r=OSDynamicCast(AirPortDWA121,p);
    if(!r||!IOUserClient::start(p))return false;
    radio=r;radio->retain();return true;
}
void DWA121AirPortClient::free()
{ if(radio){radio->release();radio=0;}IOUserClient::free(); }
IOReturn DWA121AirPortClient::clientClose()
{ terminate();return kIOReturnSuccess; }
IOReturn DWA121AirPortClient::clientDied() { return clientClose(); }
IOExternalMethod *DWA121AirPortClient::getTargetAndMethodForIndex(IOService **target,UInt32 index)
{ if(!target||index!=0)return 0;*target=this;return &method; }
IOReturn DWA121AirPortClient::command(void *in,void *out,IOByteCount n,IOByteCount *outSize)
{
    if(!in||!outSize||n<1)return kIOReturnBadArgument;
    struct proc *caller=current_proc();
    IOLog("DWA121 AirPort P02: caller=%s pid=%d op=%u in=%lu out=%lu\n",
        caller?caller->p_comm:"?",caller?caller->p_pid:-1,
        (unsigned int)*(unsigned char *)in,(unsigned long)n,(unsigned long)*outSize);
    if(isInactive()||!radio)return kIOReturnNotAttached;
    if(*(unsigned char *)in==18){
        if(n!=1)return kIOReturnBadArgument;
        return radio->readStatus(out,outSize);
    }
    if(*(unsigned char *)in!=13)return kIOReturnUnsupported;
    if(n!=1||*outSize!=0)return kIOReturnBadArgument;
    return radio->readPower();
}
