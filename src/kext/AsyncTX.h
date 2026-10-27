#ifndef DWA121_ASYNC_TX_H
#define DWA121_ASYNC_TX_H
#include "../rtl8188cu/tx.h"
#include "../usb/tx_lifecycle.h"
#include <IOKit/IOEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/usb/IOUSBPipe.h>
struct PantherTXReport {
    unsigned int submitted, completed, dataWrites, zlpWrites, bytesSubmitted;
    unsigned int succeeded, failed, cancelled, rejected, aborts, watchdogs;
    unsigned int phase, result;
    IOReturn lastError;
    bool drained, stopped;
};
/* Private kernel transport, not exposed through the user-client ABI.
 * Caller owns the hardware/session policy: TX power, channel, MAC/BSSID,
 * rates and controlled port. USB completion does NOT imply an over-air ACK.
 * All public calls/shutdown are serialized by the owner's lifecycle lock,
 * outside this workloop and the USB provider gate. */
class PantherTXEngine : public IOEventSource {
    OSDeclareDefaultStructors(PantherTXEngine)
    IOService *service;
    IOUSBPipe *highPipe, *normalPipe, *activePipe;
    IOBufferMemoryDescriptor *buffer;
    void *dmaBytes; /* stable capacity pointer, even while descriptor length is zero */
    IOWorkLoop *loop;
    IOCommandGate *gate;
    IOTimerEventSource *watchdog;
    IOLock *completionLock;
    rtl_tx_lifecycle life;
    rtl_tx_packet packet;
    PantherTXReport report;
    unsigned int maxPacket;
    bool sourceAdded, gateAdded, timerAdded, notifyEnabled, detached;
    IOReturn completionStatus;
    UInt32 completionRemaining;
    struct Request { const rtl_u8 *frame,*mac,*bssid; unsigned int length,endpoint; bool ccmp; };
    static void completed(void *,void *,IOReturn,UInt32);
    static void timedOut(OSObject *,IOTimerEventSource *);
    static IOReturn sendAction(OSObject *,void *,void *,void *,void *);
    static IOReturn snapshotAction(OSObject *,void *,void *,void *,void *);
    static IOReturn stopAction(OSObject *,void *,void *,void *,void *);
    static IOReturn drainAction(OSObject *,void *,void *,void *,void *);
    static IOReturn finishAction(OSObject *,void *,void *,void *,void *);
    bool initialize(IOService *,IOUSBPipe *,IOUSBPipe *);
    void submit();
    void consume();
    void abortWrite(bool failed);
    void recordResult();
    void detachSources();
protected:
    virtual bool checkForWork();
    virtual void free();
public:
    static PantherTXEngine *create(IOService *,IOUSBPipe *,IOUSBPipe *);
    /* Success means copied/accepted by engine; inspect snapshot for completion.
     * BUSY until takeResult retrieves the previous logical transaction result.
     * Errors poison the engine until shutdown/recreation; never retry a frame
     * whose delivery is unknown. Buffers need only live through this call. */
    IOReturn sendFrame(const rtl_u8 *,unsigned int,const rtl_u8 *,const rtl_u8 *);
    IOReturn sendCCMP(const rtl_u8 *,unsigned int,const rtl_u8 *,const rtl_u8 *);
    IOReturn sendEmpty(unsigned int endpoint);
    IOReturn snapshot(PantherTXReport *,bool takeResult=false);
    bool shutdown();
};
#endif
