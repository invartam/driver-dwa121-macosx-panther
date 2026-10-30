#ifndef DWA121_DIAGNOSTIC_H
#define DWA121_DIAGNOSTIC_H
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include "../rtl8188cu/identity.h"
#include "../control/protocol.h"
#include "../control/association.h"
#include "../control/wpa.h"
#include "../control/network.h"
#include "../net80211/scan.h"
#include "../rtl8188cu/rx.h"
class PantherTXEngine;
class IOUSBPipe;
class IOTimerEventSource;
class IOUserClient;
class DWA121ControlClient;
class PantherRXEngine;
class IOUSBInterface;
class IOWorkLoop;
struct PantherBusContext;
/* USB hardware owner; the opt-in Ethernet controller is a separate child. */
class DWA121Diagnostic : public IOService {
    OSDeclareDefaultStructors(DWA121Diagnostic)
    PantherRXEngine *liveRX;
    IOUSBPipe *liveRXPipe;
    IOTimerEventSource *scanTimer;
    bool scanTimerAdded,scanActive,scanValid;
    unsigned int scanChannel,scanHome;
    rtl_u32 scanGeneration,scanCompleted;
    rtl_bss_table *scanTable,*scanScratch;
    static void scanStep(OSObject *,IOTimerEventSource *);
    bool scanTuneLocked(unsigned int);
    void scanCancelLocked();
    IOUSBInterface *liveInterface;
    PantherBusContext *liveContext;
    struct rtl_bus liveBus;
    IOLock *lifecycleLock;
    IOWorkLoop *serviceLoop;
    DWA121ControlClient *controlOwner; /* borrowed, serialized by lifecycleLock */
    PantherTXEngine *liveTX;
    IOUSBPipe *liveHighPipe, *liveNormalPipe; /* held by open liveInterface */
    rtl_identity liveIdentity;
    rtl_u8 livePeer[6];
    unsigned int liveChannel;
    dwa_assoc_budget txBudget;
    bool wpaActive,wpaPrepared;
    bool wpaVerified,dataProbeActive;
    dwa_data_budget dataBudget;
    unsigned int wpaKeyMask,wpaSends;
    rtl_u8 wpaRSC[5][6];
    IOTimerEventSource *associationTimer;
    bool associationTimerAdded;
    dwa_net_bridge *networkBridge;
    bool networkAliveLocked(rtl_u32) const;
    bool sessionExpiredLocked(rtl_u32) const;
    static void associationExpired(OSObject *,IOTimerEventSource *);
    bool stopLiveLocked();
    bool stopLive();
    volatile bool usbTerminating;
    bool startHardwareLocked(IOService *provider);
public:
    virtual bool init(OSDictionary *dictionary = 0);
    virtual IOWorkLoop *getWorkLoop() const;
    virtual bool finalize(IOOptionBits options);
    virtual bool start(IOService *provider);
    virtual IOReturn message(UInt32 type,IOService *provider,void *argument=0);
    virtual void stop(IOService *provider);
    virtual void free();
    virtual IOReturn callPlatformFunction(const OSSymbol *,bool,void *,void *,void *,void *);
    virtual IOReturn setProperties(OSObject *properties);
    virtual IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **);
    IOReturn claimControl(DWA121ControlClient *);
    void releaseControl(DWA121ControlClient *);
    IOReturn controlCommand(DWA121ControlClient *,const dwa_control_request *,dwa_control_reply *);
    IOReturn associationCommand(DWA121ControlClient *,const dwa_assoc_request *,dwa_assoc_reply *);
    IOReturn wpaCommand(DWA121ControlClient *,const dwa_wpa_request *,dwa_assoc_reply *);
    IOReturn networkCommand(DWA121ControlClient *,const dwa_net_request *,dwa_net_reply *);
    bool networkStatus();
    bool networkTransmit(const rtl_u8 *,unsigned int);
    bool networkReceive(dwa_net_frame *);
};
#endif
