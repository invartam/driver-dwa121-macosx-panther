/* Panther-only, opt-in read-only AirPort control companion.
 * Wire evidence: docs/airport-p02-architecture.md. No Apple code included. */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSBoolean.h>
extern "C" {
#include <sys/proc.h>
#include <sys/vm.h>
}

class AirPortDWA121 : public IOService {
    OSDeclareDefaultStructors(AirPortDWA121)
    IOService *hardware;
public:
    virtual bool init(OSDictionary * = 0);
    virtual void free();
    virtual bool start(IOService *provider);
    virtual IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **);
    IOReturn readPower();
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
{ hardware=0;return IOService::init(p); }
void AirPortDWA121::free()
{ if(hardware)hardware->release();IOService::free(); }
bool AirPortDWA121::start(IOService *provider)
{
    if(!provider || !IOService::start(provider))return false;
    hardware=provider;hardware->retain();
    setProperty("P02ReadOnly",true);
    setProperty("P02Protocol","Panther WirelessGetPower / command 13");
    registerService();return true;
}
IOReturn AirPortDWA121::readPower()
{
    IOService *p=hardware;
    if(isInactive()||!p||p->isInactive())return kIOReturnNotAttached;
    OSObject *value=p->copyProperty("RadioRunning");
    if(!value)return kIOReturnNotReady;
    bool on=value==kOSBooleanTrue;
    value->release();
    IOLog("DWA121 AirPort P02: command=13 RadioRunning=%u (native return bits)\n",on?1:0);
    /* This old ABI carries the result in IOReturn, not an output buffer. */
    return on?3:0;
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
IOReturn DWA121AirPortClient::command(void *in,void *,IOByteCount n,IOByteCount *outSize)
{
    if(!in||!outSize||n<1)return kIOReturnBadArgument;
    struct proc *caller=current_proc();
    IOLog("DWA121 AirPort P02: caller=%s pid=%d op=%u in=%lu out=%lu\n",
        caller?caller->p_comm:"?",caller?caller->p_pid:-1,
        (unsigned int)*(unsigned char *)in,(unsigned long)n,(unsigned long)*outSize);
    if(isInactive()||!radio)return kIOReturnNotAttached;
    if(*(unsigned char *)in!=13)return kIOReturnUnsupported;
    if(n!=1||*outSize!=0)return kIOReturnBadArgument;
    return radio->readPower();
}
