#include "PersistentRX.h"
#include <IOKit/IOLib.h>
#include <kern/clock.h>
static rtl_u32 monotonicMS()
{
    AbsoluteTime ticks; UInt64 ns;
    clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks,&ns);
    return (rtl_u32)(ns/1000000ULL);
}
OSDefineMetaClassAndStructors(PantherRXEngine, IOEventSource)
PantherRXEngine *PantherRXEngine::create(IOService *s, IOUSBPipe *p, unsigned int c, const rtl_u8 *mac, PantherLiveObserver o)
{
    PantherRXEngine *e = new PantherRXEngine;
    if (e && !e->initialize(s,p,c,mac,o)) {
        e->detachSources(); e->release(); e = 0;
    }
    return e;
}
bool PantherRXEngine::initialize(IOService *s, IOUSBPipe *p, unsigned int c, const rtl_u8 *mac, PantherLiveObserver o)
{
    service=0; pipe=0; buffer=0; loop=0; gate=0; watchdog=0; completionLock=0;
    sourceAdded=gateAdded=timerAdded=notifyEnabled=detached=false;
    completionStatus=kIOReturnSuccess; completionRemaining=RTL_RX_BUFFER_SIZE;
    if (!IOEventSource::init(s,0)) return false;
    bzero(&life,sizeof(life)); bzero(&report,sizeof(report)); bzero(&table,sizeof(table));
    dwa_frame_queue_reset(&frames); bzero(&filter,sizeof(filter)); capture=false;
    report.usb.drained=true; channel=c; observer=o;
    if (!s || !p || !o || !mac || c<1 || c>11) return false;
    bcopy(mac,filter.mac,6);
    service=s; service->retain(); pipe=p; pipe->retain();
    completionLock=IOLockAlloc();
    buffer=IOBufferMemoryDescriptor::withCapacity(RTL_RX_BUFFER_SIZE,kIODirectionIn);
    loop=IOWorkLoop::workLoop();
    gate=IOCommandGate::commandGate(this);
    watchdog=IOTimerEventSource::timerEventSource(this,timedOut);
    if (!completionLock || !buffer || !loop || !gate || !watchdog) return false;
    if (loop->addEventSource(gate)!=kIOReturnSuccess) return false;
    gateAdded=true;
    if (loop->addEventSource(watchdog)!=kIOReturnSuccess) return false;
    timerAdded=true;
    if (loop->addEventSource(this)!=kIOReturnSuccess) return false;
    sourceAdded=true;
    IOLockLock(completionLock); notifyEnabled=true; IOLockUnlock(completionLock);
    return true;
}
void PantherRXEngine::completed(void *, void *parameter, IOReturn status, UInt32 remaining)
{
    PantherRXEngine *e=(PantherRXEngine *)parameter;
    /* No gate acquisition, parsing, USB call, or new read in this callback.
     * Abort may invoke us inline while the driver's workloop gate is held. */
    IOLockLock(e->completionLock);
    e->completionStatus=status; e->completionRemaining=remaining;
    rtl_rx_life_complete(&e->life);
    if (e->notifyEnabled) e->signalWorkAvailable();
    IOLockUnlock(e->completionLock);
    e->release(); /* accepted request owns one independent reference */
}
void PantherRXEngine::controlFrame(void *cookie, unsigned int kind, const rtl_u8 *frame, unsigned int bytes)
{
    PantherRXEngine *e=(PantherRXEngine *)cookie;
    if (kind==RTL_RX_BEACON) rtl_bss_observe_at(&e->table,frame,bytes,e->channel,monotonicMS());
    if (e->capture) dwa_frame_queue_push(&e->frames,kind,e->channel,monotonicMS(),frame,bytes);
}
IOReturn PantherRXEngine::captureAction(OSObject *object,void *enabled,void *,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)object;
    e->capture=enabled!=0; e->filter.selected=0;e->filter.encrypted=0; bzero(e->filter.bssid,6);
    dwa_frame_queue_reset(&e->frames); return kIOReturnSuccess;
}
IOReturn PantherRXEngine::setCapture(bool enabled)
{ return detached?kIOReturnNotReady:gate->runAction(captureAction,enabled?this:0); }
IOReturn PantherRXEngine::encryptedAction(OSObject *o,void *enabled,void *,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)o;
    IOLockLock(e->completionLock);bool running=e->life.running!=0;IOLockUnlock(e->completionLock);
    if(enabled&&(!e->capture||!e->filter.selected||!running))return kIOReturnNotReady;
    e->filter.encrypted=enabled?1:0;
    /* Purge pre-handshake data and any queued frames at closing. */
    dwa_frame_queue_reset(&e->frames);return kIOReturnSuccess;
}
IOReturn PantherRXEngine::setEncryptedCapture(bool enabled)
{return detached?kIOReturnNotReady:gate->runAction(encryptedAction,enabled?this:0);}
IOReturn PantherRXEngine::controlAction(OSObject *object,void *input,void *output,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)object;
    const dwa_control_request *r=(const dwa_control_request *)input;
    dwa_control_reply *out=(dwa_control_reply *)output;
    bzero(out,sizeof(*out));
    if (!e->capture || e->detached) return kIOReturnNotReady;
    if (dwa_control_validate(r,e->filter.mac)!=RTL_OK) return kIOReturnBadArgument;
    if (r->operation==DWA_CONTROL_SELECT || r->operation==DWA_CONTROL_RESET) {
        dwa_frame_queue_reset(&e->frames);
        e->filter.encrypted=0;
        e->filter.selected=r->operation==DWA_CONTROL_SELECT;
        bcopy(r->bssid,e->filter.bssid,6);
    }
    out->version=DWA_CONTROL_VERSION; out->channel=e->channel;
    IOLockLock(e->completionLock);
    if (e->life.running) out->flags|=DWA_CONTROL_RUNNING;
    IOLockUnlock(e->completionLock);
    if (e->filter.selected) out->flags|=DWA_CONTROL_SELECTED;
    bcopy(e->filter.mac,out->mac,6); bcopy(e->filter.bssid,out->bssid,6);
    if (r->operation==DWA_CONTROL_NEXT) {
        rtl_u32 now=monotonicMS();
        if (dwa_frame_queue_pop(&e->frames,now,&out->frame)==1) {
            out->flags|=DWA_CONTROL_FRAME; out->age_ms=now-out->frame.received_ms;
            out->frame.received_ms=0; /* expose age, not a private clock epoch */
        }
    }
    out->queued=e->frames.count; out->dropped=e->frames.dropped;
    out->expired=e->frames.expired; out->delivered=e->frames.delivered;
    return kIOReturnSuccess;
}
IOReturn PantherRXEngine::control(const dwa_control_request *r,dwa_control_reply *out)
{ return detached?kIOReturnNotReady:gate->runAction(controlAction,(void *)r,out); }
void PantherRXEngine::publish()
{
    IOLockLock(completionLock);
    report.running=life.running!=0;
    report.usb.drained=life.pending==0;
    IOLockUnlock(completionLock);
    observer(service,&report,&table);
}
void PantherRXEngine::submit()
{
    IOLockLock(completionLock);
    bool reserved=rtl_rx_life_reserve(&life)!=0;
    IOLockUnlock(completionLock);
    if (!reserved) return;
    bzero(buffer->getBytesNoCopy(),RTL_RX_BUFFER_SIZE);
    IOUSBCompletion completion;
    completion.target=0; completion.action=completed; completion.parameter=this;
    retain(); /* before Read: the callback is allowed to run inline */
    IOReturn armed=watchdog->setTimeoutMS(3000);
    if (armed!=kIOReturnSuccess) {
        IOLockLock(completionLock); rtl_rx_life_reject(&life); IOLockUnlock(completionLock);
        ++report.submissionsRejected; ++report.usb.usbErrors; report.usb.lastError=armed;
        release(); publish(); return;
    }
    IOReturn status=pipe->Read(buffer,1000,2000,RTL_RX_BUFFER_SIZE,&completion,0);
    if (status==kIOReturnSuccess) ++report.usb.submitted;
    else {
        watchdog->cancelTimeout();
        IOLockLock(completionLock); rtl_rx_life_reject(&life); IOLockUnlock(completionLock);
        ++report.submissionsRejected; ++report.usb.usbErrors; report.usb.lastError=status;
        release(); /* rejected submission cannot call completed */
        publish();
    }
}
void PantherRXEngine::consume()
{
    IOReturn status; UInt32 remaining; int deliver=0;
    IOLockLock(completionLock);
    bool ready=rtl_rx_life_consume(&life,&deliver)!=0;
    status=completionStatus; remaining=completionRemaining;
    IOLockUnlock(completionLock);
    if (!ready) return;
    watchdog->cancelTimeout(); ++report.usb.completed;
    if (status==kIOReturnSuccess && remaining<=RTL_RX_BUFFER_SIZE) {
        if (deliver && remaining<RTL_RX_BUFFER_SIZE)
            rtl8188cu_rx_parse_control((const rtl_u8 *)buffer->getBytesNoCopy(),
                RTL_RX_BUFFER_SIZE-remaining,&report.usb.frames,&filter,controlFrame,this);
    } else if (status==kIOReturnAborted) {
        ++report.usb.aborted;
        if (deliver) { /* an unsolicited cancellation is not a healthy link */
            ++report.usb.usbErrors; report.usb.lastError=status;
            IOLockLock(completionLock); rtl_rx_life_stop(&life); IOLockUnlock(completionLock);
        }
    } else if (status==kIOUSBTransactionTimeout || status==kIOReturnTimeout) {
        ++report.usb.timeouts;
        if (deliver) {
            IOReturn reset=pipe->ClearPipeStall(true);
            if (reset!=kIOReturnSuccess) {
                ++report.usb.usbErrors; report.usb.lastError=reset;
                IOLockLock(completionLock); rtl_rx_life_stop(&life); IOLockUnlock(completionLock);
            }
        }
    } else {
        ++report.usb.usbErrors;
        report.usb.lastError=status==kIOReturnSuccess?kIOReturnOverrun:status;
        IOLockLock(completionLock); rtl_rx_life_stop(&life); IOLockUnlock(completionLock);
    }
}
bool PantherRXEngine::checkForWork()
{
    if (detached || !completionLock) return false;
    unsigned int before=report.usb.completed;
    consume();
    submit();
    /* Diagnostic snapshots per completed USB transfer, including empty-channel
     * timeouts. No per-frame IOLog or allocation of a new DMA buffer. */
    if (report.usb.completed!=before || report.usb.usbErrors || report.stopping)
        publish();
    IOLockLock(completionLock); bool more=life.ready!=0; IOLockUnlock(completionLock);
    return more;
}
void PantherRXEngine::abortRead()
{
    IOLockLock(completionLock); bool abort=rtl_rx_life_stop(&life)!=0; IOLockUnlock(completionLock);
    if (abort) {
        ++report.abortRequests;
        IOReturn status=pipe->Abort();
        if (status!=kIOReturnSuccess) { ++report.usb.usbErrors; report.usb.lastError=status; }
    }
}
void PantherRXEngine::timedOut(OSObject *object, IOTimerEventSource *)
{
    PantherRXEngine *e=(PantherRXEngine *)object;
    IOLockLock(e->completionLock); bool hung=e->life.pending && !e->life.ready; IOLockUnlock(e->completionLock);
    if (!hung) return;
    ++e->report.watchdogs; ++e->report.usb.usbErrors; e->report.usb.lastError=kIOReturnTimeout;
    e->abortRead(); e->consume(); e->publish();
}
IOReturn PantherRXEngine::beginAction(OSObject *object,void *,void *,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)object;
    IOLockLock(e->completionLock); bool ok=rtl_rx_life_start(&e->life)!=0; IOLockUnlock(e->completionLock);
    if (!ok) return kIOReturnBusy;
    e->submit(); e->publish();
    return e->report.usb.usbErrors?kIOReturnIOError:kIOReturnSuccess;
}
bool PantherRXEngine::begin()
{return !detached && gate->runAction(beginAction)==kIOReturnSuccess;}
IOReturn PantherRXEngine::stopAction(OSObject *object,void *,void *,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)object;
    e->report.stopping=true; e->watchdog->cancelTimeout();
    captureAction(object,0,0,0,0);
    e->abortRead(); e->consume(); e->publish(); return kIOReturnSuccess;
}
IOReturn PantherRXEngine::drainAction(OSObject *object,void *,void *,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)object;
    e->consume();
    IOLockLock(e->completionLock); bool drained=e->life.pending==0; IOLockUnlock(e->completionLock);
    return drained?kIOReturnSuccess:kIOReturnBusy;
}
IOReturn PantherRXEngine::finishAction(OSObject *object,void *,void *,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)object;
    e->consume();
    IOLockLock(e->completionLock);
    e->notifyEnabled=false; e->report.usb.drained=e->life.pending==0;
    IOLockUnlock(e->completionLock);
    if (!e->report.usb.drained) { ++e->report.usb.usbErrors; e->report.usb.lastError=kIOReturnTimeout; }
    e->detached=true; e->watchdog->cancelTimeout(); e->publish();
    return kIOReturnSuccess;
}
bool PantherRXEngine::shutdown()
{
    if (detached) return report.usb.drained;
    gate->runAction(stopAction);
    bool drained=false;
    /* Do not hold the workloop gate while waiting. Completions and the event
     * source can still run; stopAction has already prohibited all new reads. */
    for (unsigned int i=0;i<500;++i) {
        if (gate->runAction(drainAction)==kIOReturnSuccess) { drained=true; break; }
        IOSleep(10);
    }
    gate->runAction(finishAction);
    drained=report.usb.drained;
    /* Block callbacks from signalling the source before removing it. The
     * callback only uses the independent completion lock and its own ref. */
    detachSources();
    publish();
    IOLog("DWA121Diagnostic: persistent RX stopped USB=%u/%u aborts=%u drained=%d errors=%u beacons=%u\n",
          report.usb.completed,report.usb.submitted,report.abortRequests,drained,
          report.usb.usbErrors,report.usb.frames.beacons);
    return drained;
}
void PantherRXEngine::detachSources()
{
    if (completionLock) {
        IOLockLock(completionLock); notifyEnabled=false; IOLockUnlock(completionLock);
    }
    if (watchdog) watchdog->cancelTimeout();
    if (loop) {
        if (sourceAdded) { loop->removeEventSource(this); sourceAdded=false; }
        if (timerAdded) { loop->removeEventSource(watchdog); timerAdded=false; }
        if (gateAdded) { loop->removeEventSource(gate); gateAdded=false; }
    }
    detached=true;
}
void PantherRXEngine::free()
{
    /* Normal shutdown removes the workloop's reference before the caller
     * releases ours. If completion is late, its reference delays this free. */
    if (watchdog) watchdog->release();
    if (gate) gate->release();
    if (loop) loop->release();
    if (buffer) buffer->release();
    if (pipe) pipe->release();
    if (completionLock) IOLockFree(completionLock);
    IOService *s=service;
    IOEventSource::free();
    if (s) s->release();
}

IOReturn PantherRXEngine::snapshotAction(OSObject *o,void *out,void *,void *,void *)
{
    PantherRXEngine *e=(PantherRXEngine *)o;
    *(rtl_bss_table *)out=e->table;return e->report.usb.usbErrors?kIOReturnIOError:kIOReturnSuccess;
}
bool PantherRXEngine::scanSnapshot(rtl_bss_table *out)
{
    if(detached){*out=table;return report.usb.usbErrors==0;}
    return gate->runAction(snapshotAction,out)==kIOReturnSuccess;
}
