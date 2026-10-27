#include "TXProbe.h"
#include <IOKit/IOLib.h>
static bool stoppedHardware(const rtl_bus *bus,rtl_u32 expectedCR)
{
    rtl_u32 cr=~0U,pause=0,security=~0U;
    return bus && bus->read &&
        !bus->read(bus->cookie,0x100,2,&cr) &&
        !bus->read(bus->cookie,0x522,1,&pause) &&
        !bus->read(bus->cookie,0x680,2,&security) &&
        cr==expectedCR && pause==0xff && security==0;
}
bool pantherTXProbe(IOService *owner,IOUSBPipe *high,IOUSBPipe *normal,const rtl_bus *bus)
{
    if(!stoppedHardware(bus,0) || !bus->write) { owner->setProperty("TXUSBProbeVerified",false); return false; }
    /* HCI_TXDMA only: admit USB OUT while TXDMA, scheduler, protocol and both
     * MAC blocks remain disabled. Leaving HCI_TXDMA off makes OUT NAK forever. */
    bool enabled=!bus->write(bus->cookie,0x100,2,1) && stoppedHardware(bus,1);
    if(!enabled) { bus->write(bus->cookie,0x100,2,0); owner->setProperty("TXUSBProbeVerified",false); return false; }
    owner->setProperty("TXUSBProbeControlAtTransfer",1,32);
    PantherTXEngine *e=PantherTXEngine::create(owner,high,normal);
    if(!e) { bus->write(bus->cookie,0x100,2,0); owner->setProperty("TXUSBProbeVerified",false); return false; }
    PantherTXReport r; bzero(&r,sizeof(r)); bool ok=true;
    for(unsigned int i=0;i<4 && ok;++i) {
        ok=e->sendEmpty(i%2?3:2)==kIOReturnSuccess;
        for(unsigned int wait=0;wait<400 && ok;++wait) {
            if(e->snapshot(&r)!=kIOReturnSuccess) { ok=false; break; }
            if(r.result) break;
            IOSleep(10);
        }
        ok=ok && r.result==RTL_TX_SUCCESS && r.drained && !r.stopped;
        if(ok) e->snapshot(&r,true);
    }
    /* Race shutdown with a fifth accepted write. Either already completed
     * successfully or cancelled; it must drain without inventing a callback. */
    if(ok) ok=e->sendEmpty(2)==kIOReturnSuccess;
    bool drained=e->shutdown();
    e->snapshot(&r); e->release();
    bool blocked=stoppedHardware(bus,1);
    bool restored=bus->write(bus->cookie,0x100,2,0)==0;
    blocked=stoppedHardware(bus,0) && blocked && restored;
    ok=ok && drained && blocked && r.submitted==5 && r.completed==5 &&
        r.zlpWrites==5 && !r.dataWrites && !r.bytesSubmitted &&
        r.succeeded+r.cancelled==5 && r.succeeded>=4 && !r.failed &&
        !r.rejected && !r.watchdogs && r.lastError==kIOReturnSuccess;
    owner->setProperty("TXUSBProbeVerified",ok);
    owner->setProperty("TXUSBProbeDrained",drained);
    owner->setProperty("TXUSBProbeNoRadioTXVerified",blocked && !r.bytesSubmitted && !r.dataWrites);
    owner->setProperty("TXUSBProbeSubmitted",r.submitted,32);
    owner->setProperty("TXUSBProbeCompleted",r.completed,32);
    owner->setProperty("TXUSBProbeZLPWrites",r.zlpWrites,32);
    owner->setProperty("TXUSBProbeDataWrites",r.dataWrites,32);
    owner->setProperty("TXUSBProbeBytesSubmitted",r.bytesSubmitted,32);
    owner->setProperty("TXUSBProbeSucceeded",r.succeeded,32);
    owner->setProperty("TXUSBProbeCancelled",r.cancelled,32);
    owner->setProperty("TXUSBProbeFailed",r.failed,32);
    owner->setProperty("TXUSBProbeRejected",r.rejected,32);
    owner->setProperty("TXUSBProbeWatchdogs",r.watchdogs,32);
    owner->setProperty("TXUSBProbeAbortRequests",r.aborts,32);
    owner->setProperty("TXUSBProbeLastError",(UInt32)r.lastError,32);
    IOLog("DWA121Diagnostic: USB TX empty probe ok=%d USB=%u/%u ZLP=%u data=%u bytes=%u success=%u cancel=%u fail=%u aborts=%u watchdogs=%u drained=%d error=0x%08lx\n",
        ok,r.completed,r.submitted,r.zlpWrites,r.dataWrites,r.bytesSubmitted,r.succeeded,
        r.cancelled,r.failed,r.aborts,r.watchdogs,drained,(unsigned long)r.lastError);
    return ok;
}
