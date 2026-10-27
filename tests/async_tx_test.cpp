#include "../src/kext/AsyncTX.h"
#include <stdio.h>
unsigned int fakeObjects=0,fakeBuffers=0,fakeTimerError=0;
std::vector<IOWorkLoop *> IOWorkLoop::loops;
std::vector<IOTimerEventSource *> IOTimerEventSource::timers;
static const unsigned char mac[]={2,1,2,3,4,5},ap[]={2,6,7,8,9,10};
struct Fixture {
    IOService *owner;
    IOUSBPipe *high,*normal;
    PantherTXEngine *engine;
    Fixture(unsigned int mps) {
        assert(!fakeObjects && !fakeBuffers);
        owner=new IOService; high=new IOUSBPipe(2,mps); normal=new IOUSBPipe(3,mps);
        engine=PantherTXEngine::create(owner,high,normal); assert(engine && fakeBuffers==1);
    }
    ~Fixture() {
        if(engine) { assert(engine->shutdown()); engine->release(); }
        high->release(); normal->release(); owner->release();
        assert(!fakeObjects && !fakeBuffers);
    }
};
static unsigned int association(unsigned char *f,unsigned int length)
{
    assert(length>=28 && length<=512);
    memset(f,0,512); memcpy(f+4,ap,6); memcpy(f+10,mac,6); memcpy(f+16,ap,6);
    f[24]=0x11; f[26]=10;
    /* Valid bounded TLVs; the transport deliberately leaves RSN policy to its
     * caller. Build exact USB boundary sizes for DATA -> ZLP testing. */
    unsigned int pos=28;
    while(pos<length) {
        unsigned int count=length-pos-2; if(count>255) count=255;
        assert(length-pos>=2); f[pos]=221; f[pos+1]=(unsigned char)count; pos+=2+count;
    }
    return length;
}
static PantherTXReport snap(Fixture &f,bool take=false)
{
    PantherTXReport r; assert(f.engine->snapshot(&r,take)==0); return r;
}
int main()
{
    unsigned char frame[512]; unsigned int modes;
    for(modes=0;modes<2;++modes) {
        Fixture f(modes?512:64);
        unsigned int length=association(frame,modes?480:32);
        assert(f.engine->sendEmpty(1)==kIOReturnBadArgument && !f.high->writes);
        assert(f.engine->sendFrame(frame,length,mac,ap)==0);
        memset(frame,0xff,sizeof(frame)); /* caller storage no longer needed */
        assert(f.engine->sendEmpty(3)==kIOReturnBusy);
        assert(f.high->pending && f.high->length==length+32);
        f.high->complete(); IOWorkLoop::pump();
        PantherTXReport r=snap(f);
        assert(!r.result && f.high->pending && f.high->length==0 && f.high->writes==2);
        assert(f.engine->sendEmpty(2)==kIOReturnBusy);
        f.high->complete(); IOWorkLoop::pump(); r=snap(f,true);
        assert(r.result==RTL_TX_SUCCESS && r.drained && r.submitted==2 && r.completed==2);
        assert(r.dataWrites==1 && r.zlpWrites==1 && r.bytesSubmitted==length+32);
        assert(f.engine->sendEmpty(3)==0); f.normal->complete(); r=snap(f,true);
        assert(r.result==RTL_TX_SUCCESS && r.succeeded==2 && r.submitted==3);
    }
    /* Snapshot consumption itself must advance the DATA -> ZLP chain, even
     * when the work event has not run. Inline completion must not free DMA. */
    {
        Fixture f(64); association(frame,32);
        f.high->inlineCompletion=true;
        assert(f.engine->sendFrame(frame,32,mac,ap)==0);
        PantherTXReport r=snap(f); assert(!r.result && f.high->writes==2);
        r=snap(f,true); assert(r.result==RTL_TX_SUCCESS && r.completed==2);
    }
    for(modes=0;modes<6;++modes) {
        Fixture f(64); association(frame,32);
        if(modes==0) f.high->reject=true;
        if(modes==1) fakeTimerError=1;
        IOReturn status=f.engine->sendFrame(frame,32,mac,ap);
        if(modes<2) assert(status==kIOReturnIOError);
        else {
            assert(status==0);
            if(modes==2) f.high->complete(0,1); /* partial success is failure */
            if(modes==3) { f.high->reject=true; f.high->complete(); } /* rejected ZLP */
            if(modes==4) { f.high->complete(); IOWorkLoop::pump(); f.high->complete(kIOReturnIOError); }
            if(modes==5) { fakeTimerError=1; f.high->complete(); } /* cannot arm ZLP watchdog */
            IOWorkLoop::pump();
        }
        PantherTXReport r=snap(f,true);
        assert(r.result==RTL_TX_FAILED && r.failed==1 && r.stopped && r.drained);
        assert(f.engine->sendEmpty(2)==kIOReturnNotReady);
    }
    {
        Fixture f(512); unsigned char key[99]; unsigned int n=0;
        memset(frame,0,sizeof(frame)); frame[0]=0xb0; frame[26]=1;
        memcpy(frame+4,ap,6); memcpy(frame+10,mac,6); memcpy(frame+16,ap,6);
        assert(f.engine->sendFrame(frame,30,mac,ap)==0);
        f.high->complete(); PantherTXReport r=snap(f,true);
        assert(r.result==RTL_TX_SUCCESS && r.dataWrites==1 && !r.zlpWrites && r.bytesSubmitted==62);
        memset(key,0,sizeof(key)); key[0]=2; key[1]=3; key[3]=95; key[4]=2;
        assert(rtl8188cu_eapol_frame(frame,sizeof(frame),&n,mac,ap,0,key,sizeof(key))==0);
        assert(f.engine->sendFrame(frame,n,mac,ap)==0 && f.normal->pending);
        f.normal->complete(); r=snap(f,true);
        assert(r.result==RTL_TX_SUCCESS && r.succeeded==2 && r.dataWrites==2 && !r.zlpWrites);
    }
    {
        Fixture f(64); association(frame,32); assert(f.engine->sendFrame(frame,32,mac,ap)==0);
        IOTimerEventSource::timers[0]->fire();
        PantherTXReport r=snap(f);
        assert(r.result==RTL_TX_FAILED && r.watchdogs==1 && r.aborts==1 && r.drained);
        assert(f.high->writes==1); /* timeout never submits trailing ZLP */
    }
    {
        Fixture f(512); association(frame,480); assert(f.engine->sendFrame(frame,480,mac,ap)==0);
        assert(f.engine->shutdown());
        PantherTXReport r=snap(f);
        assert(r.result==RTL_TX_CANCELLED && r.aborts==1 && r.completed==1 && f.high->writes==1);
        assert(f.engine->shutdown()); assert(snap(f).aborts==1);
    }
    {
        Fixture f(64); association(frame,32); f.high->abortInline=false;
        assert(f.engine->sendFrame(frame,32,mac,ap)==0);
        assert(!f.engine->shutdown());
        assert(!snap(f).drained && fakeBuffers==1);
        f.engine->release(); f.engine=0; /* external owner gone; DMA still pinned */
        assert(fakeBuffers==1 && f.high->pending);
        f.high->complete(kIOReturnAborted,64); /* late callback releases resources */
        assert(fakeBuffers==0);
    }
    {
        IOService *s=new IOService; IOUSBPipe *a=new IOUSBPipe(2,64),*b=new IOUSBPipe(3,512);
        assert(!PantherTXEngine::create(s,a,b)); a->release(); b->release(); s->release();
        assert(!fakeObjects);
    }
    for(unsigned int mps=64;mps<=512;mps*=8) {
        Fixture f(mps);unsigned int n=mps==64?96:480;
        memset(frame,0,sizeof(frame));frame[0]=8;frame[1]=0x41;frame[24]=1;frame[27]=0x20;
        memcpy(frame+4,ap,6);memcpy(frame+10,mac,6);memset(frame+16,255,6);
        assert(f.engine->sendFrame(frame,n,mac,ap)!=0);
        assert(!f.engine->sendCCMP(frame,n,mac,ap)&&f.normal->pending);
        assert(f.engine->sendCCMP(frame,n,mac,ap)==kIOReturnBusy);
        f.normal->complete();IOWorkLoop::pump();assert(f.normal->pending);
        f.normal->complete();PantherTXReport r=snap(f,true);
        assert(r.result==RTL_TX_SUCCESS&&r.dataWrites==1&&r.zlpWrites==1&&r.drained);
    }
    puts("PASS: real AsyncTX.cpp with deterministic IOKit doubles: USB64/512 DATA+ZLP, buffer copy, busy gate, inline callbacks, partial writes, timer/rejection/ZLP errors, stop and late callback lifetime; separate CCMP path");
    return 0;
}
