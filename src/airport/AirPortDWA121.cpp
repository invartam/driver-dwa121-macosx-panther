#include "../control/airport_status.h"
/* Panther-only, opt-in AirPort status and passive scan companion.
 * Wire evidence: docs/airport-p02-architecture.md. No Apple code included. */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <kern/task.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <kern/clock.h>
#include "../net80211/scan.h"
#include "../control/airport_protocol.h"
extern "C" {
#include <sys/proc.h>
#include <sys/vm.h>
}

static rtl_u32 nativeScanNow();
static void secretWipe(void *v,unsigned int n)
{volatile unsigned char *p=(volatile unsigned char *)v;while(n--)*p++=0;}
class AirPortDWA121 : public IOService {
    OSDeclareDefaultStructors(AirPortDWA121)
    IOService *hardware,*controller;
    OSObject *originalClass;
    IOLock *lock;
    void detachHardware();
    IOWorkLoop *loop;
    IOTimerEventSource *timer;
    void *broker;
    rtl_u32 brokerPulse;
    dwa_airport_reply pending;
    bool cancelPending,recovering;
    static void tick(OSObject *,IOTimerEventSource *);
    IOReturn core(const char *,void *,dwa_airport_target *);
public:
    virtual bool init(OSDictionary * = 0);
    virtual IOWorkLoop *getWorkLoop() const {return loop;}
    IOReturn brokerOpen(void *);
    void brokerClose(void *);
    IOReturn brokerCommand(void *,const dwa_airport_request *,dwa_airport_reply *);
    IOReturn select(const dwa_join_selection *,const unsigned char *,unsigned int,rtl_u32 *);
    IOReturn waitJoin(rtl_u32);
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
    bool snapshotReady,isBroker;
    unsigned char secret[64];
    unsigned int secretLength;
    rtl_u32 secretTime;
    IOTimerEventSource *secretTimer;
    bool secretTimerAdded;
    static void expireSecret(OSObject *,IOTimerEventSource *);
    IOReturn secretCommand(unsigned int,void *,IOByteCount,rtl_u32 *);
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
    hardware=controller=0;originalClass=0;lock=0;loop=0;timer=0;broker=0;
    brokerPulse=0;cancelPending=recovering=false;bzero(&pending,sizeof(pending));
    if(!IOService::init(p))return false;
    lock=IOLockAlloc();loop=IOWorkLoop::workLoop();
    if(!lock||!loop)return false;
    timer=IOTimerEventSource::timerEventSource(this,tick);
    return timer&&loop->addEventSource(timer)==kIOReturnSuccess;
}
void AirPortDWA121::free()
{ detachHardware();if(timer){timer->cancelTimeout();if(loop)loop->removeEventSource(timer);timer->release();}
  if(loop)loop->release();secretWipe(&pending,sizeof(pending));if(lock)IOLockFree(lock);IOService::free(); }
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
    setProperty("P02ReadOnly",false);
    timer->setTimeoutMS(250);
    setProperty("P031RSSIAvailable",false);
    setProperty("P02Protocol","Panther status 13,18 / scan 8,9 / WPA2 selection 26,17");
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
    hardware=controller=0;originalClass=0;broker=0;cancelPending=false;secretWipe(&pending,sizeof(pending));
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
/* Native port status and identity come from one locked engine snapshot.
 * Connected means verified WPA2/data port/heartbeat, never DHCP or acceptance.
 * The native command carries no generation; no identity is cached here. */
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
    dwa_airport_target target;bzero(&target,sizeof(target));
    IOLockLock(lock);IOReturn state=core(DWA_AIRPORT_STATUS,0,&target);IOLockUnlock(lock);
    /* Calibration holds the engine lock. Publish unknown/non-associated,
     * never cached Connected. Panther treats an API failure as non-client
     * (Internet Sharing) and can retain that mode after recovery. */
    if(state==kIOReturnBusy)bzero(&target,sizeof(target));
    else if(state!=kIOReturnSuccess){value->release();bzero(out,56);return state;}
    bool valid=dwa_airport_status(&target,(const rtl_u8 *)mac->getBytesNoCopy(),(rtl_u8 *)out,56)!=0;
    value->release();*size=56;
    if(!valid)return kIOReturnNotReady;
    return kIOReturnSuccess;
}
IOReturn AirPortDWA121::newUserClient(task_t task,void *security,UInt32 type,IOUserClient **out)
{
    if(!out)return kIOReturnBadArgument;
    *out=0;
    if(type!=12&&type!=DWA_AIRPORT_BROKER_TYPE)return kIOReturnUnsupported;
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
    isBroker=type==DWA_AIRPORT_BROKER_TYPE;secretLength=0;secretTime=0;secretTimer=0;secretTimerAdded=false;bzero(secret,sizeof(secret));
    if((type!=12&&!isBroker)||(isBroker&&clientHasPrivilege(task,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)||
       !IOUserClient::initWithTask(task,security,type))return false;
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
    radio=r;radio->retain();
    if(isBroker)return radio->brokerOpen(this)==kIOReturnSuccess;
    secretTimer=IOTimerEventSource::timerEventSource(this,expireSecret);
    if(!secretTimer||radio->getWorkLoop()->addEventSource(secretTimer)!=kIOReturnSuccess)return false;
    secretTimerAdded=true;return true;
}
void DWA121AirPortClient::free()
{
    if(isBroker&&radio)radio->brokerClose(this);
    if(secretTimer){secretTimer->cancelTimeout();if(secretTimerAdded&&radio)radio->getWorkLoop()->removeEventSource(secretTimer);secretTimer->release();}
    secretWipe(secret,sizeof(secret));
    if(snapshot)IOFree(snapshot,sizeof(*snapshot));
    if(scanLock)IOLockFree(scanLock);
    if(radio){radio->release();radio=0;}IOUserClient::free();
}
IOReturn DWA121AirPortClient::clientClose()
{ if(isBroker&&radio)radio->brokerClose(this);
  IOLockLock(scanLock);secretWipe(secret,sizeof(secret));secretLength=0;if(secretTimer)secretTimer->cancelTimeout();IOLockUnlock(scanLock);
  terminate();return kIOReturnSuccess; }
IOReturn DWA121AirPortClient::clientDied() { return clientClose(); }
IOExternalMethod *DWA121AirPortClient::getTargetAndMethodForIndex(IOService **target,UInt32 index)
{ if(!target||index!=0)return 0;*target=this;return &method; }
IOReturn DWA121AirPortClient::command(void *in,void *out,IOByteCount n,IOByteCount *outSize)
{
    if(!in||!outSize||n<1)return kIOReturnBadArgument;
    if(isBroker){
        if(n!=sizeof(dwa_airport_request)||*outSize!=sizeof(dwa_airport_reply)||!out)return kIOReturnBadArgument;
        bzero(out,sizeof(dwa_airport_reply));
        if(clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
        return radio?radio->brokerCommand(this,(const dwa_airport_request *)in,(dwa_airport_reply *)out):kIOReturnNotAttached;
    }
    struct proc *caller=current_proc();
    if(*(unsigned char *)in!=9&&*(unsigned char *)in!=13)
    IOLog("DWA121 AirPort P02: caller=%s pid=%d op=%u in=%lu out=%lu\n",
        caller?caller->p_comm:"?",caller?caller->p_pid:-1,
        (unsigned int)*(unsigned char *)in,(unsigned long)n,(unsigned long)*outSize);
    if(isInactive()||!radio)return kIOReturnNotAttached;
    unsigned int op=*(unsigned char *)in;
    if(op==17||op==26){
        if(*outSize!=0)return kIOReturnBadArgument;
        IOLockLock(scanLock);
        rtl_u32 joinGeneration=0;
        IOReturn r=secretCommand(op,(unsigned char *)in+1,n-1,&joinGeneration);
        IOLockUnlock(scanLock);
        if(op==26)secretWipe(in,n);
        if(r==kIOReturnSuccess&&joinGeneration)r=radio->waitJoin(joinGeneration);
        return r;
    }
    if(op==8||op==9){
        if((op==8&&(n!=34||*outSize!=0))||
           (op==9&&(n!=5||*outSize!=50||!out)))return kIOReturnBadArgument;
        IOLockLock(scanLock);
        IOReturn r=scanCommand(op,(unsigned char *)in+1,out);
        IOLockUnlock(scanLock);return r;
    }
    /* WirelessGetInfo2 requires command 3 only when port status is 4.
     * Bounds 0/92 are the original framework's normalized-quality request.
     * PHY metrics remain unavailable (legacy zero fields); no RSSI conversion
     * or positive signal strength is claimed. This is strictly read-only. */
    if(op==3){
        const unsigned char *p=(const unsigned char *)in+1;
        if(!out||n!=9||*outSize!=12||p[0]||p[1]||p[2]||p[3]||p[4]||p[5]||p[6]||p[7]!=92)return kIOReturnBadArgument;
        /* These unavailable metrics make no claim about association. A link
         * ending between 18 and 3 must not turn the API into AP/server mode. */
        bzero(out,12);return kIOReturnSuccess;
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

IOReturn AirPortDWA121::core(const char *name,void *in,dwa_airport_target *out)
{
    if(!hardware||hardware->isInactive()||isInactive())return kIOReturnNotAttached;
    return hardware->callPlatformFunction(name,false,in,out,0,0);
}
IOReturn AirPortDWA121::brokerOpen(void *owner)
{
    IOLockLock(lock);IOReturn r=kIOReturnExclusiveAccess;
    if(!broker&&!cancelPending){broker=owner;brokerPulse=nativeScanNow();r=kIOReturnSuccess;}
    IOLockUnlock(lock);return r;
}
void AirPortDWA121::brokerClose(void *owner)
{
    IOLockLock(lock);
    if(broker==owner){broker=0;secretWipe(pending.secret,sizeof(pending.secret));pending.secret_length=0;
        if(pending.target.generation)cancelPending=true;}
    IOLockUnlock(lock);
}
void AirPortDWA121::tick(OSObject *owner,IOTimerEventSource *sender)
{
    AirPortDWA121 *r=(AirPortDWA121 *)owner;
    IOLockLock(r->lock);
    if(r->broker&&!r->recovering&&(rtl_u32)(nativeScanNow()-r->brokerPulse)>5000){
        r->broker=0;secretWipe(r->pending.secret,sizeof(r->pending.secret));r->pending.secret_length=0;
        if(r->pending.target.generation)r->cancelPending=true;
    }
    if(r->cancelPending){
        dwa_airport_target out;bzero(&out,sizeof(out));
        IOReturn result=r->core(DWA_AIRPORT_CANCEL,&r->pending.target.generation,&out);
        if(result!=kIOReturnBusy){r->cancelPending=false;r->pending.target=out;}
    }
    IOLockUnlock(r->lock);if(!r->isInactive())sender->setTimeoutMS(250);
}
IOReturn AirPortDWA121::select(const dwa_join_selection *s,const unsigned char *secret,unsigned int length,rtl_u32 *generation)
{
    if(!IOLockTryLock(lock))return kIOReturnBusy;
    IOReturn result=kIOReturnNotReady;
    do {
        if(s->operation==DWA_JOIN_DISCONNECT){
            secretWipe(pending.secret,sizeof(pending.secret));pending.secret_length=0;
            if(pending.target.generation)cancelPending=true;
            result=kIOReturnSuccess;break;
        }
        if(!broker||recovering||cancelPending||(rtl_u32)(nativeScanNow()-brokerPulse)>5000)break;
        if(s->legacy_prompt!=2||length<8||length>63){result=kIOReturnUnsupported;break;}
        dwa_airport_target target;bzero(&target,sizeof(target));
        result=core(DWA_AIRPORT_RESERVE,(void *)s,&target);
        if(result!=kIOReturnSuccess)break;
        secretWipe(&pending,sizeof(pending));pending.target=target;
        pending.secret_length=length;bcopy(secret,pending.secret,length);*generation=target.generation;
        IOLog("DWA121 AirPort: native selection accepted session=%lu SSIDLength=%u\n",
              (unsigned long)target.generation,s->ssid_length);
    }while(false);
    IOLockUnlock(lock);return result;
}
IOReturn AirPortDWA121::brokerCommand(void *owner,const dwa_airport_request *request,dwa_airport_reply *out)
{
    if(request->version!=DWA_AIRPORT_VERSION||request->reserved||request->operation<1||request->operation>4)return kIOReturnBadArgument;
    IOLockLock(lock);IOReturn result=kIOReturnNotPermitted;
    do {
        if(broker!=owner)break;
        brokerPulse=nativeScanNow();
        if(request->operation==DWA_AP_RECOVER){
            if(pending.secret_length||cancelPending){result=kIOReturnBusy;break;}
            IOService *h=hardware;if(!h){result=kIOReturnNotAttached;break;}
            h->retain();recovering=true;IOLockUnlock(lock);
            result=h->callPlatformFunction(DWA_AIRPORT_RECOVER,false,0,&out->target,0,0);h->release();
            IOLockLock(lock);recovering=false;brokerPulse=nativeScanNow();break;
        }
        result=core(DWA_AIRPORT_STATUS,0,&out->target);
        if(result!=kIOReturnSuccess)break;
        if(request->operation==DWA_AP_POLL)break;
        if(request->generation!=pending.target.generation||request->generation!=out->target.generation){result=kIOReturnNotReady;break;}
        if(request->operation==DWA_AP_CANCEL){
            secretWipe(pending.secret,sizeof(pending.secret));pending.secret_length=0;
            cancelPending=true;result=kIOReturnSuccess;break;
        }
        if(cancelPending||!pending.secret_length||out->target.phase!=DWA_AP_REQUESTED){result=kIOReturnNotReady;break;}
        result=core(DWA_AIRPORT_PREPARE,&pending.target.generation,&out->target);
        if(result!=kIOReturnSuccess){secretWipe(pending.secret,sizeof(pending.secret));pending.secret_length=0;cancelPending=true;break;}
        out->secret_length=pending.secret_length;bcopy(pending.secret,out->secret,pending.secret_length);
        secretWipe(pending.secret,sizeof(pending.secret));pending.secret_length=0;pending.target=out->target;
    }while(false);
    IOLockUnlock(lock);return result;
}
void DWA121AirPortClient::expireSecret(OSObject *owner,IOTimerEventSource *)
{
    DWA121AirPortClient *c=(DWA121AirPortClient *)owner;
    IOLockLock(c->scanLock);secretWipe(c->secret,sizeof(c->secret));c->secretLength=0;IOLockUnlock(c->scanLock);
}
IOReturn DWA121AirPortClient::secretCommand(unsigned int op,void *input,IOByteCount n,rtl_u32 *generation)
{
    const unsigned char *p=(const unsigned char *)input;
    if(op==26){
        secretWipe(secret,sizeof(secret));secretLength=0;secretTimer->cancelTimeout();
        if(n<9||n>64||p[n-1])return kIOReturnBadArgument;
        for(unsigned int i=0;i<n-1;++i)if(p[i]<32||p[i]>126)return kIOReturnBadArgument;
        bcopy(p,secret,n-1);secretLength=n-1;secretTime=nativeScanNow();
        IOReturn r=secretTimer->setTimeoutMS(10000);
        if(r!=kIOReturnSuccess){secretWipe(secret,sizeof(secret));secretLength=0;}
        return r;
    }
    dwa_join_selection selection;IOReturn result=kIOReturnBadArgument;
    if((rtl_u32)(nativeScanNow()-secretTime)>=10000){secretWipe(secret,sizeof(secret));secretLength=0;}
    if(dwa_join_parse(p,(unsigned int)n,&selection)){
        result=selection.operation==DWA_JOIN_DISCONNECT||selection.legacy_prompt==2?
            radio->select(&selection,secret,secretLength,generation):kIOReturnUnsupported;
    }
    secretWipe(secret,sizeof(secret));secretLength=0;secretTimer->cancelTimeout();
    return result;
}

/* ClientController joinWEP: dispatches threadedWirelessJoinWEP: on NSThread.
 * The native framework otherwise waits only 20 x 100 ms, too short for WPA2.
 * Wait on that calling thread, never a workloop and never with any lock held.
 * Failure/cancel completes too; command 18 carries the actual native result. */
IOReturn AirPortDWA121::waitJoin(rtl_u32 generation)
{
    rtl_u32 start=nativeScanNow();
    while((rtl_u32)(nativeScanNow()-start)<15000){
        dwa_airport_target target;bzero(&target,sizeof(target));
        IOLockLock(lock);IOReturn r=core(DWA_AIRPORT_STATUS,0,&target);IOLockUnlock(lock);
        if(r==kIOReturnSuccess){
            if(target.generation!=generation)return kIOReturnNotReady;
            if(target.phase==DWA_AP_CONNECTED||target.phase==DWA_AP_FAILED||target.phase==DWA_AP_DISCONNECTED)return kIOReturnSuccess;
        }else if(r!=kIOReturnBusy)return r;
        IOSleep(50);
    }
    IOLockLock(lock);
    if(pending.target.generation==generation){
        secretWipe(pending.secret,sizeof(pending.secret));pending.secret_length=0;cancelPending=true;
    }
    IOLockUnlock(lock);return kIOReturnTimeout;
}
