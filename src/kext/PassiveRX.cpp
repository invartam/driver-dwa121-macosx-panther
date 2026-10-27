#include "PassiveRX.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "../usb/rtl_wire.h"
struct RXRequest {
    IOLock *lock;
    IOBufferMemoryDescriptor *buffer;
    IOUSBPipe *pipe;
    IOService *owner;
    unsigned int references;
    bool done;
    IOReturn status;
    UInt32 remaining;
};
static void releaseRequest(RXRequest *r)
{
    bool destroy;
    IOLockLock(r->lock);
    destroy = --r->references == 0;
    IOLockUnlock(r->lock);
    if (destroy) {
        r->buffer->release(); r->pipe->release();
        IOService *owner = r->owner;
        IOLockFree(r->lock);
        IOFree(r, sizeof(*r));
        owner->release();
    }
}
static void completed(void *, void *parameter, IOReturn status, UInt32 remaining)
{
    RXRequest *r = (RXRequest *)parameter;
    IOLockLock(r->lock);
    r->status = status; r->remaining = remaining; r->done = true;
    IOLockUnlock(r->lock);
    releaseRequest(r);
}
static bool isDone(RXRequest *r)
{
    bool done;
    IOLockLock(r->lock); done = r->done; IOLockUnlock(r->lock);
    return done;
}
struct BeaconContext {
    struct rtl_bss_table *table;
    unsigned int channel;
};
static void observeBeacon(void *cookie, const rtl_u8 *frame, unsigned int size)
{
    BeaconContext *context = (BeaconContext *)cookie;
    rtl_bss_observe(context->table, frame, size, context->channel);
}
static int receive(IOService *owner, IOUSBPipe *pipe, PantherRXReport *report, bool abortNow,
                   BeaconContext *beacons)
{
    RXRequest *r = (RXRequest *)IOMalloc(sizeof(RXRequest));
    IOUSBCompletion completion;
    IOReturn status;
    unsigned int n, bytes;
    if (!r) return RTL_IO;
    bzero(r, sizeof(*r));
    r->lock = IOLockAlloc();
    r->buffer = IOBufferMemoryDescriptor::withCapacity(RTL_RX_BUFFER_SIZE, kIODirectionIn);
    if (!r->lock || !r->buffer) {
        if (r->lock) IOLockFree(r->lock);
        if (r->buffer) r->buffer->release();
        IOFree(r, sizeof(*r)); return RTL_IO;
    }
    r->pipe = pipe; pipe->retain(); r->owner = owner; owner->retain();
    bzero(r->buffer->getBytesNoCopy(), RTL_RX_BUFFER_SIZE);
    r->references = 2; /* caller and USB completion, including inline completion */
    completion.target = 0; completion.action = completed; completion.parameter = r;
    status = pipe->Read(r->buffer, 1000, 2000, RTL_RX_BUFFER_SIZE, &completion, 0);
    if (status != kIOReturnSuccess) {
        /* IOUSBPipe returns an immediate error without scheduling completion. */
        report->lastError = status; ++report->usbErrors;
        releaseRequest(r); releaseRequest(r); return RTL_IO;
    }
    ++report->submitted;
    if (abortNow) {
        status = pipe->Abort();
        if (status != kIOReturnSuccess) { report->lastError = status; ++report->usbErrors; }
    }
    for (n = 0; n < 300 && !isDone(r); ++n) IOSleep(10);
    if (!isDone(r)) {
        pipe->Abort();
        for (n = 0; n < 200 && !isDone(r); ++n) IOSleep(10);
    }
    if (!isDone(r)) {
        /* Drop only caller ownership. Keep DMA memory and module alive until
         * the controller finally calls back; never free an in-flight buffer. */
        report->drained = false; report->lastError = kIOReturnTimeout;
        ++report->usbErrors; releaseRequest(r); return RTL_TIMEOUT;
    }
    ++report->completed;
    status = r->status;
    IOLog("DWA121Diagnostic: RX USB completion status=0x%08lx remaining=%lu abortProbe=%d\n",
          (unsigned long)status, (unsigned long)r->remaining, abortNow);
    if (!abortNow && r->remaining <= RTL_RX_BUFFER_SIZE - 24) {
        const rtl_u8 *descriptor = (const rtl_u8 *)r->buffer->getBytesNoCopy();
        IOLog("DWA121Diagnostic: RX first descriptor %08lx %08lx %08lx %08lx %08lx %08lx\n",
              (unsigned long)rtl_get_le32(descriptor), (unsigned long)rtl_get_le32(descriptor+4),
              (unsigned long)rtl_get_le32(descriptor+8), (unsigned long)rtl_get_le32(descriptor+12),
              (unsigned long)rtl_get_le32(descriptor+16), (unsigned long)rtl_get_le32(descriptor+20));
        if (status == kIOUSBTransactionTimeout || status == kIOReturnTimeout) {
            struct rtl_rx_stats partial;
            bzero(&partial, sizeof(partial));
            int parsed = rtl8188cu_rx_parse(descriptor, RTL_RX_BUFFER_SIZE-r->remaining, &partial);
            IOLog("DWA121Diagnostic: RX partial diagnostic parsed=%d descriptors=%u beacons=%u FCS=%u malformed=%u\n",
                  parsed, partial.descriptors, partial.beacons, partial.fcs_errors, partial.malformed);
        }
    }
    if (status == kIOReturnSuccess && r->remaining <= RTL_RX_BUFFER_SIZE) {
        bytes = RTL_RX_BUFFER_SIZE - r->remaining;
        if (bytes && !abortNow)
            rtl8188cu_rx_parse_beacons((const rtl_u8 *)r->buffer->getBytesNoCopy(),bytes,
                &report->frames, beacons ? observeBeacon : 0, beacons);
    } else if (status == kIOUSBTransactionTimeout || status == kIOReturnTimeout) {
        ++report->timeouts;
        /* Timeouts may desynchronize the host toggle; resynchronize explicitly. */
        IOReturn reset = pipe->ClearPipeStall(true);
        if (reset != kIOReturnSuccess) { report->lastError = reset; ++report->usbErrors; }
    } else if (status == kIOReturnAborted) ++report->aborted;
    else {
        ++report->usbErrors;
        report->lastError = status == kIOReturnSuccess ? kIOReturnOverrun : status;
    }
    releaseRequest(r);
    return report->usbErrors ? RTL_IO : RTL_OK;
}
int pantherReceiveProbe(IOService *owner, IOUSBPipe *pipe, PantherRXReport *report,
                        struct rtl_bss_table *table, unsigned int channel, unsigned int reads)
{
    unsigned int i;
    int result;
    BeaconContext beacons;
    if (!table || channel < 1 || channel > 11 || !reads || reads > 64)
        return RTL_BAD_ARGUMENT;
    beacons.table = table; beacons.channel = channel;
    bzero(table, sizeof(*table));
    bzero(report,sizeof(*report)); report->drained = true;
    for (i = 0; i < reads; ++i) {
        result = receive(owner,pipe,report,false,&beacons);
        if (result) return result;
        if (report->timeouts >= 2 && !report->frames.accepted) break;
    }
    return RTL_OK;
}
int pantherAbortProbe(IOService *owner, IOUSBPipe *pipe, PantherRXReport *report)
{
    return receive(owner,pipe,report,true,0);
}
