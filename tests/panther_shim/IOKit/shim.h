#ifndef DWA_TEST_PANTHER_SHIM_H
#define DWA_TEST_PANTHER_SHIM_H
/* Test-only deterministic IOKit substitute. Compile the REAL AsyncTX.cpp
 * against this interface to test buffer/refcount ownership and USB failures.
 * Not a model of hardware, the scheduler, or real mutual exclusion. */
#include <assert.h>
#include <string.h>
#include <vector>
typedef unsigned int UInt32;
typedef int IOReturn;
enum { kIOReturnSuccess=0,kIOReturnBadArgument=-1,kIOReturnBusy=-2,
       kIOReturnNotReady=-3,kIOReturnUnderrun=-4,kIOReturnTimeout=-5,
       kIOReturnAborted=-6,kIOReturnIOError=-7,kUSBOut=0,kUSBIn=1,kUSBBulk=2,
       kIODirectionOut=0 };
#define OSDeclareDefaultStructors(name)
#define OSDefineMetaClassAndStructors(name,base)
extern unsigned int fakeObjects,fakeBuffers,fakeTimerError;
class OSObject {
    unsigned int refs;
public:
    OSObject():refs(1) { ++fakeObjects; }
    virtual ~OSObject() { --fakeObjects; }
    void retain() { assert(refs); ++refs; }
    void release() { assert(refs); if(!--refs) free(); }
    virtual void free() { delete this; }
};
class IOService : public OSObject {};
class IOEventSource : public OSObject {
protected:
    OSObject *owner;
    virtual bool checkForWork() { return false; }
public:
    bool signalled;
    IOEventSource():owner(0),signalled(false) {}
    bool init(OSObject *o,void *) { owner=o; return true; }
    void signalWorkAvailable() { signalled=true; }
    void dispatch() { if(signalled) { signalled=false; if(checkForWork()) signalled=true; } }
};
class IOWorkLoop : public OSObject {
public:
    std::vector<IOEventSource *> sources;
    static std::vector<IOWorkLoop *> loops;
    static IOWorkLoop *workLoop() { IOWorkLoop *p=new IOWorkLoop; loops.push_back(p); return p; }
    IOReturn addEventSource(IOEventSource *s) { sources.push_back(s); s->retain(); return 0; }
    void removeEventSource(IOEventSource *s) {
        for(unsigned int i=0;i<sources.size();++i) if(sources[i]==s) {
            sources.erase(sources.begin()+i); s->release(); return;
        }
        assert(false);
    }
    static void pump() {
        for(unsigned int pass=0;pass<4;++pass)
            for(unsigned int i=0;i<loops.size();++i)
                for(unsigned int j=0;j<loops[i]->sources.size();++j) loops[i]->sources[j]->dispatch();
    }
    virtual ~IOWorkLoop() {
        assert(sources.empty());
        for(unsigned int i=0;i<loops.size();++i) if(loops[i]==this) { loops.erase(loops.begin()+i); break; }
    }
};
typedef IOReturn (*FakeAction)(OSObject *,void *,void *,void *,void *);
class IOCommandGate : public IOEventSource {
public:
    static IOCommandGate *commandGate(OSObject *o) { IOCommandGate *g=new IOCommandGate; g->owner=o; return g; }
    IOReturn runAction(FakeAction a,void *p=0,void *q=0,void *r=0,void *s=0) { return a(owner,p,q,r,s); }
};
class IOTimerEventSource;
typedef void (*FakeTimerAction)(OSObject *,IOTimerEventSource *);
class IOTimerEventSource : public IOEventSource {
    FakeTimerAction action;
public:
    bool armed;
    static std::vector<IOTimerEventSource *> timers;
    static IOTimerEventSource *timerEventSource(OSObject *o,FakeTimerAction a) {
        IOTimerEventSource *t=new IOTimerEventSource; t->owner=o; t->action=a; t->armed=false;
        timers.push_back(t); return t;
    }
    IOReturn setTimeoutMS(unsigned int ms) {
        assert(ms==3000);
        if(fakeTimerError) { --fakeTimerError; return kIOReturnIOError; }
        armed=true; return 0;
    }
    void cancelTimeout() { armed=false; }
    void fire() { if(armed) { armed=false; action(owner,this); } }
    virtual ~IOTimerEventSource() {
        for(unsigned int i=0;i<timers.size();++i) if(timers[i]==this) { timers.erase(timers.begin()+i); break; }
    }
};
struct IOLock { bool held; };
inline IOLock *IOLockAlloc() { IOLock *p=new IOLock; p->held=false; return p; }
inline void IOLockFree(IOLock *p) { assert(!p->held); delete p; }
inline void IOLockLock(IOLock *p) { assert(!p->held); p->held=true; }
inline void IOLockUnlock(IOLock *p) { assert(p->held); p->held=false; }
inline void IOSleep(unsigned int) {} /* deterministic late-callback/drain timeout */
class IOBufferMemoryDescriptor : public OSObject {
    std::vector<unsigned char> bytes;
    unsigned int length;
public:
    static IOBufferMemoryDescriptor *withCapacity(unsigned int n,int) {
        IOBufferMemoryDescriptor *b=new IOBufferMemoryDescriptor; b->bytes.resize(n); b->length=n; ++fakeBuffers; return b;
    }
    virtual ~IOBufferMemoryDescriptor() { --fakeBuffers; }
    void *getBytesNoCopy() { return length?&bytes[0]:0; }
    unsigned int getLength() { return length; }
    void setLength(unsigned int n) { assert(n<=bytes.size()); length=n; }
};
struct IOUSBCompletion {
    void *target;
    void (*action)(void *,void *,IOReturn,UInt32);
    void *parameter;
};
class IOUSBPipe : public OSObject {
public:
    unsigned int endpoint,mps,writes,aborts,length;
    bool pending,inlineCompletion,abortInline,reject;
    IOReturn callbackStatus;
    UInt32 callbackRemaining;
    IOUSBCompletion completion;
    IOBufferMemoryDescriptor *dma;
    std::vector<unsigned int> lengths;
    std::vector<unsigned char> submittedBytes;
    IOUSBPipe(unsigned int ep,unsigned int size):endpoint(ep),mps(size),writes(0),aborts(0),length(0),
        pending(false),inlineCompletion(false),abortInline(true),reject(false),
        callbackStatus(0),callbackRemaining(0),dma(0) {}
    unsigned int GetEndpointNumber() { return endpoint; }
    unsigned int GetMaxPacketSize() { return mps; }
    unsigned int GetType() { return kUSBBulk; }
    unsigned int GetDirection() { return kUSBOut; }
    IOReturn Write(IOBufferMemoryDescriptor *b,unsigned int noData,unsigned int deadline,
                   unsigned int n,IOUSBCompletion *c) {
        assert(!pending && noData==1000 && deadline==2000 && n<=b->getLength());
        n=b->getLength(); /* Panther EHCI uses descriptor length, not reqCount */
        if(reject) return kIOReturnIOError;
        ++writes; lengths.push_back(n); length=n; dma=b; completion=*c; pending=true;
        unsigned char *p=(unsigned char *)b->getBytesNoCopy();
        if(n) submittedBytes.assign(p,p+n); else submittedBytes.clear();
        if(inlineCompletion) complete(callbackStatus,callbackRemaining);
        return 0;
    }
    void complete(IOReturn status=0,UInt32 remaining=0) {
        assert(pending);
        /* A real controller still owns/reads this DMA pointer until callback. */
        unsigned char *p=(unsigned char *)dma->getBytesNoCopy();
        for(unsigned int i=0;i<length;++i) assert(p[i]==submittedBytes[i]);
        IOUSBCompletion c=completion; pending=false; dma=0;
        c.action(c.target,c.parameter,status,remaining);
    }
    IOReturn Abort() {
        ++aborts; if(pending && abortInline) complete(kIOReturnAborted,length);
        return 0;
    }
    virtual ~IOUSBPipe() { assert(!pending); }
};
#endif
