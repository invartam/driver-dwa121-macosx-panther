#ifndef DWA121_PERSISTENT_RX_H
#define DWA121_PERSISTENT_RX_H
#include "PassiveRX.h"
#include "../usb/rx_lifecycle.h"
#include "../control/protocol.h"
#include <IOKit/IOEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
struct PantherLiveReport {
    PantherRXReport usb;
    unsigned int watchdogs, abortRequests, submissionsRejected;
    bool running, stopping;
};
typedef void (*PantherLiveObserver)(IOService *, const PantherLiveReport *, const rtl_bss_table *);
class PantherRXEngine : public IOEventSource {
    OSDeclareDefaultStructors(PantherRXEngine)
    IOService *service;
    IOUSBPipe *pipe;
    IOBufferMemoryDescriptor *buffer;
    IOWorkLoop *loop;
    IOCommandGate *gate;
    IOTimerEventSource *watchdog;
    IOLock *completionLock;
    rtl_rx_lifecycle life;
    PantherLiveReport report;
    rtl_bss_table table;
    dwa_frame_queue frames;
    rtl_rx_filter filter;
    bool capture;
    PantherLiveObserver observer;
    unsigned int channel;
    bool sourceAdded, gateAdded, timerAdded, notifyEnabled, detached;
    IOReturn completionStatus;
    UInt32 completionRemaining;
    static void completed(void *, void *, IOReturn, UInt32);
    static void timedOut(OSObject *, IOTimerEventSource *);
    static void controlFrame(void *, unsigned int, const rtl_u8 *, unsigned int);
    static IOReturn controlAction(OSObject *, void *, void *, void *, void *);
    static IOReturn snapshotAction(OSObject *,void *,void *,void *,void *);
    static IOReturn captureAction(OSObject *, void *, void *, void *, void *);
    static IOReturn encryptedAction(OSObject *,void *,void *,void *,void *);
    static IOReturn beginAction(OSObject *, void *, void *, void *, void *);
    static IOReturn stopAction(OSObject *, void *, void *, void *, void *);
    static IOReturn finishAction(OSObject *, void *, void *, void *, void *);
    static IOReturn drainAction(OSObject *, void *, void *, void *, void *);
    bool initialize(IOService *, IOUSBPipe *, unsigned int, const rtl_u8 *, PantherLiveObserver);
    void submit();
    void consume();
    void abortRead();
    void publish();
    void detachSources();
protected:
    virtual bool checkForWork();
    virtual void free();
public:
    static PantherRXEngine *create(IOService *, IOUSBPipe *, unsigned int, const rtl_u8 *, PantherLiveObserver);
    IOReturn control(const dwa_control_request *, dwa_control_reply *);
    IOReturn setCapture(bool);
    IOReturn setEncryptedCapture(bool);
    bool begin();
    bool scanSnapshot(rtl_bss_table *);
    /* Must be called outside this engine's workloop; prevents rearming first,
     * aborts once, then waits outside the gate. Idempotent. Late callbacks own
     * the engine/DMA buffer/pipe/service until return, even if drain times out. */
    bool shutdown();
};
#endif
