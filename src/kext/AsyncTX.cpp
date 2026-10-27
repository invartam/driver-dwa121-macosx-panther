#include "AsyncTX.h"
#include <IOKit/IOLib.h>
OSDefineMetaClassAndStructors(PantherTXEngine,IOEventSource)
PantherTXEngine *PantherTXEngine::create(IOService *s,IOUSBPipe *high,IOUSBPipe *normal)
{
    PantherTXEngine *e=new PantherTXEngine;
    if(e && !e->initialize(s,high,normal)) { e->detachSources(); e->release(); e=0; }
    return e;
}
bool PantherTXEngine::initialize(IOService *s,IOUSBPipe *high,IOUSBPipe *normal)
{
    service=0; highPipe=normalPipe=activePipe=0; buffer=0; dmaBytes=0; loop=0; gate=0;
    watchdog=0; completionLock=0; maxPacket=0;
    sourceAdded=gateAdded=timerAdded=notifyEnabled=detached=false;
    completionStatus=kIOReturnSuccess; completionRemaining=0;
    bzero(&life,sizeof(life)); bzero(&packet,sizeof(packet)); bzero(&report,sizeof(report));
    report.drained=true;
    if(!IOEventSource::init(s,0) || !s || !high || !normal) return false;
    maxPacket=high->GetMaxPacketSize();
    if((maxPacket!=64 && maxPacket!=512) || normal->GetMaxPacketSize()!=maxPacket ||
       high->GetType()!=kUSBBulk || normal->GetType()!=kUSBBulk ||
       high->GetDirection()!=kUSBOut || normal->GetDirection()!=kUSBOut ||
       high->GetEndpointNumber()!=2 || normal->GetEndpointNumber()!=3) return false;
    service=s; service->retain(); highPipe=high; highPipe->retain();
    normalPipe=normal; normalPipe->retain();
    completionLock=IOLockAlloc();
    buffer=IOBufferMemoryDescriptor::withCapacity(RTL_TX_USB_MAX,kIODirectionOut);
    if(buffer) dmaBytes=buffer->getBytesNoCopy();
    loop=IOWorkLoop::workLoop(); gate=IOCommandGate::commandGate(this);
    watchdog=IOTimerEventSource::timerEventSource(this,timedOut);
    if(!completionLock || !buffer || !dmaBytes || !loop || !gate || !watchdog) return false;
    if(loop->addEventSource(gate)!=kIOReturnSuccess) return false;
    gateAdded=true;
    if(loop->addEventSource(watchdog)!=kIOReturnSuccess) return false;
    timerAdded=true;
    if(loop->addEventSource(this)!=kIOReturnSuccess) return false;
    sourceAdded=true; notifyEnabled=true;
    return true;
}
void PantherTXEngine::completed(void *,void *parameter,IOReturn status,UInt32 remaining)
{
    PantherTXEngine *e=(PantherTXEngine *)parameter;
    /* Abort may callback inline. Never acquire a gate or touch DMA here. */
    IOLockLock(e->completionLock);
    e->completionStatus=status; e->completionRemaining=remaining;
    rtl_tx_life_complete(&e->life);
    if(e->notifyEnabled) e->signalWorkAvailable();
    IOLockUnlock(e->completionLock);
    e->release();
}
void PantherTXEngine::recordResult()
{
    /* Called once on transition to DONE, including submission rejection. */
    if(life.phase!=RTL_TX_DONE || report.result) return;
    report.result=life.result;
    if(life.result==RTL_TX_SUCCESS) ++report.succeeded;
    else if(life.result==RTL_TX_CANCELLED) ++report.cancelled;
    else ++report.failed;
    /* No outstanding DMA when a transaction reaches DONE. */
    bzero(dmaBytes,RTL_TX_USB_MAX); bzero(&packet,sizeof(packet));
}
void PantherTXEngine::submit()
{
    IOLockLock(completionLock);
    bool reserved=rtl_tx_life_reserve(&life)!=0;
    unsigned int phase=life.phase;
    IOLockUnlock(completionLock);
    if(!reserved) return;
    IOUSBCompletion completion;
    completion.target=0; completion.action=completed; completion.parameter=this;
    unsigned int length=phase==RTL_TX_DATA?packet.length:0;
    /* Panther EHCI uses the descriptor length for bulk transfers, even when
     * reqCount differs. Keep BOTH lengths exact, including zero for ZLP. Only change
     * descriptor geometry between requests, never while DMA is pending. */
    buffer->setLength(length);
    retain(); /* request reference exists even if completion arrives inline */
    IOReturn status=watchdog->setTimeoutMS(3000);
    if(status==kIOReturnSuccess)
        status=activePipe->Write(buffer,1000,2000,length,&completion);
    if(status==kIOReturnSuccess) {
        ++report.submitted; report.bytesSubmitted+=length;
        if(phase==RTL_TX_DATA) ++report.dataWrites; else ++report.zlpWrites;
    } else {
        watchdog->cancelTimeout();
        IOLockLock(completionLock); rtl_tx_life_reject(&life); IOLockUnlock(completionLock);
        ++report.rejected; report.lastError=status;
        release(); /* rejected submission never owns a callback */
        recordResult();
    }
}
void PantherTXEngine::consume()
{
    IOLockLock(completionLock);
    bool ready=life.ready!=0, stopped=life.stopped!=0;
    IOReturn status=completionStatus; UInt32 remaining=completionRemaining;
    if(ready) rtl_tx_life_consume(&life,status==kIOReturnSuccess && remaining==0);
    IOLockUnlock(completionLock);
    if(!ready) return;
    watchdog->cancelTimeout(); ++report.completed;
    if(!stopped && (status!=kIOReturnSuccess || remaining))
        report.lastError=status==kIOReturnSuccess?kIOReturnUnderrun:status;
    recordResult();
}
bool PantherTXEngine::checkForWork()
{
    if(detached || !completionLock) return false;
    consume(); submit();
    IOLockLock(completionLock); bool more=life.ready!=0; IOLockUnlock(completionLock);
    return more;
}
IOReturn PantherTXEngine::sendAction(OSObject *object,void *arg,void *,void *,void *)
{
    PantherTXEngine *e=(PantherTXEngine *)object;
    Request *r=(Request *)arg;
    if(e->detached || e->life.stopped) return kIOReturnNotReady;
    if(e->life.phase!=RTL_TX_IDLE) return kIOReturnBusy;
    if(r->frame) {
        int prepared=r->ccmp ? rtl8188cu_tx_prepare_ccmp(&e->packet,r->frame,r->length,r->mac,r->bssid,e->maxPacket) :
            rtl8188cu_tx_prepare(&e->packet,r->frame,r->length,r->mac,r->bssid,e->maxPacket);
        if(prepared!=RTL_OK)
            return kIOReturnBadArgument;
    } else {
        if(r->endpoint!=2 && r->endpoint!=3) return kIOReturnBadArgument;
        bzero(&e->packet,sizeof(e->packet)); e->packet.endpoint=r->endpoint;
    }
    e->activePipe=e->packet.endpoint==2?e->highPipe:e->normalPipe;
    bzero(e->dmaBytes,RTL_TX_USB_MAX);
    bcopy(e->packet.bytes,e->dmaBytes,e->packet.length);
    IOLockLock(e->completionLock);
    bool began=rtl_tx_life_begin(&e->life,e->packet.needs_zlp,r->frame==0)!=0;
    IOLockUnlock(e->completionLock);
    if(!began) return kIOReturnBusy;
    e->report.result=0; e->report.lastError=kIOReturnSuccess;
    e->submit(); return e->report.result==RTL_TX_FAILED?e->report.lastError:kIOReturnSuccess;
}
IOReturn PantherTXEngine::sendFrame(const rtl_u8 *f,unsigned int n,const rtl_u8 *mac,const rtl_u8 *ap)
{
    if(detached || !f) return kIOReturnBadArgument;
    Request r; r.frame=f; r.length=n; r.mac=mac; r.bssid=ap; r.endpoint=0;r.ccmp=false;
    return gate->runAction(sendAction,&r);
}
IOReturn PantherTXEngine::sendCCMP(const rtl_u8 *f,unsigned int n,const rtl_u8 *mac,const rtl_u8 *ap)
{
    if(detached||!f)return kIOReturnBadArgument;
    Request r;r.frame=f;r.length=n;r.mac=mac;r.bssid=ap;r.endpoint=0;r.ccmp=true;
    return gate->runAction(sendAction,&r);
}
IOReturn PantherTXEngine::sendEmpty(unsigned int endpoint)
{
    if(detached) return kIOReturnNotReady;
    Request r; bzero(&r,sizeof(r)); r.endpoint=endpoint;
    return gate->runAction(sendAction,&r);
}
IOReturn PantherTXEngine::snapshotAction(OSObject *object,void *out,void *take,void *,void *)
{
    PantherTXEngine *e=(PantherTXEngine *)object;
    e->consume(); /* may leave DATA -> ZLP; workloop submits it */
    IOLockLock(e->completionLock);
    e->report.phase=e->life.phase; e->report.drained=!e->life.pending;
    e->report.stopped=e->life.stopped!=0;
    *(PantherTXReport *)out=e->report;
    if(take && rtl_tx_life_take(&e->life)) e->report.result=0;
    IOLockUnlock(e->completionLock);
    /* Consuming a DATA completion here must not lose its work signal. */
    e->submit();
    return kIOReturnSuccess;
}
IOReturn PantherTXEngine::snapshot(PantherTXReport *out,bool take)
{
    if(!out) return kIOReturnBadArgument;
    if(detached) { *out=report; return kIOReturnSuccess; }
    return gate->runAction(snapshotAction,out,take?this:0);
}
void PantherTXEngine::abortWrite(bool failed)
{
    IOLockLock(completionLock); bool abort=rtl_tx_life_stop(&life,failed)!=0; IOLockUnlock(completionLock);
    if(abort) {
        ++report.aborts;
        IOReturn status=activePipe->Abort();
        if(status!=kIOReturnSuccess && report.lastError==kIOReturnSuccess) report.lastError=status;
    }
    recordResult();
}
void PantherTXEngine::timedOut(OSObject *object,IOTimerEventSource *)
{
    PantherTXEngine *e=(PantherTXEngine *)object;
    IOLockLock(e->completionLock); bool hung=e->life.pending && !e->life.ready; IOLockUnlock(e->completionLock);
    if(!hung) return;
    ++e->report.watchdogs; e->report.lastError=kIOReturnTimeout;
    e->abortWrite(true); e->consume();
}
IOReturn PantherTXEngine::stopAction(OSObject *object,void *,void *,void *,void *)
{
    PantherTXEngine *e=(PantherTXEngine *)object;
    e->watchdog->cancelTimeout(); e->abortWrite(false); e->consume();
    return kIOReturnSuccess;
}
IOReturn PantherTXEngine::drainAction(OSObject *object,void *,void *,void *,void *)
{
    PantherTXEngine *e=(PantherTXEngine *)object;
    e->consume();
    IOLockLock(e->completionLock); bool drained=!e->life.pending; IOLockUnlock(e->completionLock);
    return drained?kIOReturnSuccess:kIOReturnBusy;
}
IOReturn PantherTXEngine::finishAction(OSObject *object,void *,void *,void *,void *)
{
    PantherTXEngine *e=(PantherTXEngine *)object;
    e->consume();
    IOLockLock(e->completionLock);
    e->notifyEnabled=false; e->report.drained=!e->life.pending;
    e->report.phase=e->life.phase; e->report.stopped=e->life.stopped!=0;
    IOLockUnlock(e->completionLock);
    e->watchdog->cancelTimeout(); e->detached=true;
    return kIOReturnSuccess;
}
bool PantherTXEngine::shutdown()
{
    if(detached) return report.drained;
    gate->runAction(stopAction);
    for(unsigned int i=0;i<500;++i) {
        if(gate->runAction(drainAction)==kIOReturnSuccess) break;
        IOSleep(10);
    }
    gate->runAction(finishAction); detachSources();
    /* If Abort did not drain, callback's reference still owns buffer/pipe/
     * service. Never fake completion or free the DMA buffer after a timeout. */
    return report.drained;
}
void PantherTXEngine::detachSources()
{
    if(completionLock) { IOLockLock(completionLock); notifyEnabled=false; IOLockUnlock(completionLock); }
    if(watchdog) watchdog->cancelTimeout();
    if(loop) {
        if(sourceAdded) { loop->removeEventSource(this); sourceAdded=false; }
        if(timerAdded) { loop->removeEventSource(watchdog); timerAdded=false; }
        if(gateAdded) { loop->removeEventSource(gate); gateAdded=false; }
    }
    detached=true;
}
void PantherTXEngine::free()
{
    if(watchdog) watchdog->release();
    if(gate) gate->release();
    if(loop) loop->release();
    if(buffer) { if(dmaBytes) bzero(dmaBytes,RTL_TX_USB_MAX); buffer->release(); }
    bzero(&packet,sizeof(packet));
    if(highPipe) highPipe->release();
    if(normalPipe) normalPipe->release();
    if(completionLock) IOLockFree(completionLock);
    IOService *s=service; IOEventSource::free(); if(s) s->release();
}
