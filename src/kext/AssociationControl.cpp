#include "DWA121Diagnostic.h"
#include "AsyncTX.h"
#include "PersistentRX.h"
#include "../rtl8188cu/station_hw.h"
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <libkern/c++/OSBoolean.h>
static rtl_u32 associationMS()
{
    AbsoluteTime ticks; UInt64 ns;
    clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks,&ns);
    return (rtl_u32)(ns/1000000ULL);
}
static void replyTX(dwa_assoc_reply *out,const PantherTXReport *r)
{
    out->version=DWA_ASSOC_VERSION;
    out->flags=(r->drained?DWA_ASSOC_DRAINED:0)|(r->stopped?DWA_ASSOC_STOPPED:DWA_ASSOC_ACTIVE);
    out->result=r->result; out->submitted=r->submitted; out->completed=r->completed;
    out->succeeded=r->succeeded; out->failed=r->failed; out->cancelled=r->cancelled;
    out->bytes=r->bytesSubmitted; out->aborts=r->aborts; out->watchdogs=r->watchdogs;
    out->last_error=(rtl_u32)r->lastError;
}
void DWA121Diagnostic::associationExpired(OSObject *owner,IOTimerEventSource *)
{
    DWA121Diagnostic *d=(DWA121Diagnostic *)owner;
    IOLockLock(d->lifecycleLock);
    if(d->usbTerminating||d->liveTX) {
        d->setProperty("AssociationDeadlineExpired",true);
        d->stopLiveLocked();
    }
    IOLockUnlock(d->lifecycleLock);
}
IOReturn DWA121Diagnostic::associationCommand(DWA121ControlClient *client,
    const dwa_assoc_request *r,dwa_assoc_reply *out)
{
    IOLockLock(lifecycleLock);
    IOReturn result=kIOReturnNotReady;
    dwa_control_reply *rx=0;
    PantherTXReport tx; bzero(&tx,sizeof(tx));
    do {
        if(controlOwner!=client || !liveRX || isInactive()) break;
        if(dwa_assoc_validate(r,liveIdentity.mac)!=RTL_OK) { result=kIOReturnBadArgument; break; }
        if(getProperty("AssociationTXEnabled")!=kOSBooleanTrue) { result=kIOReturnUnsupported; break; }
        if(r->operation==DWA_ASSOC_END) {
            if(!liveTX) break;
            rtl_u32 mgq=0,dma=0,beq=0;
            int qerror=liveBus.read(liveBus.cookie,0x410,4,&mgq);
            int derror=liveBus.read(liveBus.cookie,0x210,4,&dma);
            int berror=liveBus.read(liveBus.cookie,0x408,4,&beq);
            setProperty("AssociationBEQAtEnd",beq,32);
            setProperty("AssociationMGQAtEnd",mgq,32);
            setProperty("AssociationDMAStatusAtEnd",dma,32);
            IOLog("DWA121Diagnostic: association before block MGQ=%08lx BEQ=%08lx DMA=%08lx errors=%d/%d/%d\n",
                  (unsigned long)mgq,(unsigned long)beq,(unsigned long)dma,qerror,berror,derror);
            bool blocked=rtl8188cu_station_hw_block(&liveBus)==RTL_OK;
            bool drained=liveTX->shutdown(); liveTX->snapshot(&tx); replyTX(out,&tx);
            bool stopped=stopLiveLocked();
            result=blocked && drained && stopped ? kIOReturnSuccess : kIOReturnIOError;
            break;
        }
        if(liveTX && sessionExpiredLocked(associationMS())) {
            stopLiveLocked(); result=kIOReturnTimeout; break;
        }
        rx=(dwa_control_reply *)IOMalloc(sizeof(*rx));
        if(!rx) { result=kIOReturnNoMemory; break; }
        dwa_control_request status; bzero(&status,sizeof(status));
        status.version=DWA_CONTROL_VERSION; status.operation=DWA_CONTROL_STATUS;
        if(liveRX->control(&status,rx)!=kIOReturnSuccess || !(rx->flags&DWA_CONTROL_RUNNING)) {
            if(liveTX) stopLiveLocked(); break;
        }
        if(r->operation==DWA_ASSOC_BEGIN) {
            if(liveTX) { result=kIOReturnBusy; break; }
            if(r->channel!=liveChannel || !(rx->flags&DWA_CONTROL_SELECTED) ||
               memcmp(r->bssid,rx->bssid,6)) { result=kIOReturnBadArgument; break; }
            /* Allocate transport before enabling any hardware TX. */
            liveTX=PantherTXEngine::create(this,liveHighPipe,liveNormalPipe);
            if(!liveTX) { result=kIOReturnNoMemory; break; }
            bcopy(r->bssid,livePeer,6); bzero(&txBudget,sizeof(txBudget));
            if(getProperty("WPA2ProbeEnabled")==kOSBooleanTrue) {
                wpaPrepared=true; /* cleanup also applies to a failed clear */
                int cleared=rtl8188cu_station_hw_wpa_prepare(&liveBus);
                setProperty("WPA2CAMPrepared",cleared==RTL_OK);
                if(cleared!=RTL_OK){stopLiveLocked();result=kIOReturnIOError;break;}
            }
            txBudget.start_ms=associationMS();
            int configured=rtl8188cu_station_hw_begin(&liveBus,&liveIdentity,livePeer,liveChannel);
            setProperty("AssociationTXConfigured",configured==RTL_OK);
            IOLog("DWA121Diagnostic: association management TX configuration result=%d channel=%u\n",configured,liveChannel);
            if(configured!=RTL_OK) { stopLiveLocked(); result=kIOReturnIOError; break; }
            if(associationTimer->setTimeoutMS(DWA_ASSOC_MAX_MS)!=kIOReturnSuccess) {
                stopLiveLocked(); result=kIOReturnIOError; break;
            }
        } else {
            if(!liveTX) break;
            if(r->operation==DWA_ASSOC_SEND) {
                if(wpaActive) { result=kIOReturnNotPermitted; break; }
                if(dwa_assoc_admit(&txBudget,associationMS(),r->frame[0])!=RTL_OK) {
                    result=kIOReturnNotPermitted; break;
                }
                result=liveTX->sendFrame(r->frame,r->length,liveIdentity.mac,livePeer);
                if(result!=kIOReturnSuccess) {
                    if(result!=kIOReturnBusy && result!=kIOReturnBadArgument) stopLiveLocked();
                    break;
                }
                if(r->frame[0]==0xb0) ++txBudget.auth; else ++txBudget.assoc;
            }
        }
        /* POLL consumes a terminal USB result exactly once. SEND does not. */
        result=liveTX->snapshot(&tx,r->operation==DWA_ASSOC_POLL);
        replyTX(out,&tx);
        if(result!=kIOReturnSuccess || tx.failed || tx.stopped) {
            stopLiveLocked(); out->flags=(out->flags&~DWA_ASSOC_ACTIVE)|DWA_ASSOC_STOPPED;
            if(result==kIOReturnSuccess) result=kIOReturnIOError;
        }
    } while(false);
    if(rx) IOFree(rx,sizeof(*rx));
    IOLockUnlock(lifecycleLock); return result;
}
IOReturn DWA121Diagnostic::wpaCommand(DWA121ControlClient *client,
    const dwa_wpa_request *r,dwa_assoc_reply *out)
{
    IOLockLock(lifecycleLock);
    IOReturn result=kIOReturnNotReady;PantherTXReport tx;bzero(&tx,sizeof(tx));
    do {
        if(controlOwner!=client||!liveRX||!liveTX||isInactive())break;
        if(getProperty("WPA2ProbeEnabled")!=kOSBooleanTrue){result=kIOReturnUnsupported;break;}
        if(dwa_wpa_validate(r)!=RTL_OK){result=kIOReturnBadArgument;break;}
        rtl_u32 elapsed=associationMS()-txBudget.start_ms;
        if(sessionExpiredLocked(associationMS())){
            stopLiveLocked();result=kIOReturnTimeout;break;
        }
        result=liveTX->snapshot(&tx);
        if(result!=kIOReturnSuccess||tx.failed||tx.stopped){stopLiveLocked();result=kIOReturnIOError;break;}
        if(!tx.drained||tx.phase!=RTL_TX_IDLE){result=kIOReturnBusy;break;}
        if(r->operation==DWA_WPA_ENTER) {
            if(wpaActive||!wpaPrepared||!txBudget.assoc||tx.succeeded<2){result=kIOReturnNotPermitted;break;}
            /* Mark before any CAM access so failed setup also erases slots. */
            wpaActive=true;wpaKeyMask=0;wpaSends=0;bzero(wpaRSC,sizeof(wpaRSC));
            if(associationTimer->setTimeoutMS(DWA_WPA_MAX_MS-elapsed)!=kIOReturnSuccess||
               rtl8188cu_station_hw_eapol(&liveBus)!=RTL_OK){stopLiveLocked();result=kIOReturnIOError;break;}
            setProperty("WPA2ProbeActive",true);setProperty("WPA2ProbeAID",r->aid,32);
            setProperty("WPA2DataPortAuthorized",false);
        } else {
            if(!wpaActive){result=kIOReturnNotReady;break;}
            if(r->operation>=DWA_WPA_DATA_OPEN) {
                if(getProperty("CCMPDataProbeEnabled")!=kOSBooleanTrue||!wpaVerified){result=kIOReturnNotPermitted;break;}
                if(r->operation==DWA_WPA_DATA_OPEN) {
                    if(dataProbeActive){result=kIOReturnNotPermitted;break;}
                    rtl_u32 sec=1,cr=0,pause=0;
                    if(liveBus.read(liveBus.cookie,0x680,2,&sec)||sec||
                       liveBus.read(liveBus.cookie,0x100,2,&cr)||cr!=0x2ff||
                       liveBus.read(liveBus.cookie,0x522,1,&pause)||pause!=0x6b||
                       liveRX->setEncryptedCapture(true)!=kIOReturnSuccess){
                        stopLiveLocked();result=kIOReturnIOError;break;
                    }
                    dataProbeActive=true;bzero(&dataBudget,sizeof(dataBudget));
                    setProperty("CCMPDataProbeActive",true);setProperty("CCMPSoftwareCrypto",true);
                } else {
                    if(!dataProbeActive){result=kIOReturnNotPermitted;break;}
                    if(r->operation==DWA_WPA_DATA_SEND) {
                        rtl_u32 limit=networkAliveLocked(associationMS())?0xffffffffU:DWA_WPA_DATA_MAX_SEND;
                        if(dwa_wpa_data_reserve_limit(&dataBudget,r,liveIdentity.mac,livePeer,limit)!=RTL_OK){result=kIOReturnBadArgument;break;}
                        result=liveTX->sendCCMP(r->data,r->length,liveIdentity.mac,livePeer);
                        if(result!=kIOReturnSuccess){stopLiveLocked();break;}
                        setProperty("CCMPDataSubmitted",dataBudget.sent,32);
                    } else {
                        if(!dataBudget.sent){result=kIOReturnNotPermitted;break;}
                        setProperty("CCMPDHCPOfferReported",true);
                    }
                }
            } else if(r->operation==DWA_WPA_SEND) {
                if(dataProbeActive){result=kIOReturnNotPermitted;break;}
                if(wpaSends>=DWA_WPA_MAX_SEND){result=kIOReturnNotPermitted;break;}
                result=liveTX->sendFrame(r->data,r->length,liveIdentity.mac,livePeer);
                if(result!=kIOReturnSuccess){
                    if(result!=kIOReturnBadArgument&&result!=kIOReturnBusy)stopLiveLocked();
                    break;
                }
                ++wpaSends;setProperty("WPA2EAPOLSubmitted",wpaSends,32);
            } else if(r->operation==DWA_WPA_KEY) {
                if(wpaVerified){result=kIOReturnNotPermitted;break;}
                unsigned int slot=r->pairwise?4:r->index;
                if((wpaKeyMask&(1U<<slot))||!wpaSends){result=kIOReturnNotPermitted;break;}
                if(rtl8188cu_station_hw_key(&liveBus,r->pairwise,r->index,livePeer,r->data)!=RTL_OK){
                    stopLiveLocked();result=kIOReturnIOError;break;
                }
                wpaKeyMask|=1U<<slot;bcopy(r->rsc,wpaRSC[slot],6);
                setProperty("WPA2KeysInstalledMask",wpaKeyMask,32);
            } else {
                if(!(wpaKeyMask&16)||!(wpaKeyMask&15)||wpaSends<2){result=kIOReturnNotPermitted;break;}
                setProperty("WPA2HandshakeReported",true);
                wpaVerified=true;
            }
        }
        result=liveTX->snapshot(&tx);replyTX(out,&tx);
    }while(false);
    IOLockUnlock(lifecycleLock);return result;
}
