#include "DWA121Diagnostic.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <kern/clock.h>
#include <libkern/c++/OSBoolean.h>
static rtl_u32 netMS()
{
    AbsoluteTime ticks;UInt64 ns;clock_get_uptime(&ticks);
    absolutetime_to_nanoseconds(ticks,&ns);return (rtl_u32)(ns/1000000ULL);
}
bool DWA121Diagnostic::networkAliveLocked(rtl_u32 now) const
{ return controlOwner&&liveRX&&liveTX&&wpaVerified&&dataProbeActive&&!usbTerminating&&!isInactive()&&dwa_net_active(networkBridge,now); }
bool DWA121Diagnostic::sessionExpiredLocked(rtl_u32 now) const
{
    if(networkBridge&&networkBridge->active)return !networkAliveLocked(now);
    return (rtl_u32)(now-txBudget.start_ms)>=(wpaActive?DWA_WPA_MAX_MS:DWA_ASSOC_MAX_MS);
}
bool DWA121Diagnostic::networkStatus()
{
    IOLockLock(lifecycleLock);bool active=networkAliveLocked(netMS());
    IOLockUnlock(lifecycleLock);return active;
}
bool DWA121Diagnostic::networkTransmit(const rtl_u8 *frame,unsigned int n)
{
    IOLockLock(lifecycleLock);rtl_u32 now=netMS();
    bool accepted=networkAliveLocked(now)&&dwa_net_push(networkBridge,0,frame,n,now);
    IOLockUnlock(lifecycleLock);return accepted;
}
bool DWA121Diagnostic::networkReceive(dwa_net_frame *frame)
{
    IOLockLock(lifecycleLock);rtl_u32 now=netMS();
    bool got=networkAliveLocked(now)&&dwa_net_pop(networkBridge,1,frame,now);
    IOLockUnlock(lifecycleLock);return got;
}
IOReturn DWA121Diagnostic::networkCommand(DWA121ControlClient *client,const dwa_net_request *r,dwa_net_reply *out)
{
    if(!dwa_net_validate(r))return kIOReturnBadArgument;
    IOLockLock(lifecycleLock);IOReturn result=kIOReturnNotPermitted;rtl_u32 now=netMS();
    do {
        if(controlOwner!=client||getProperty("NetworkSessionEnabled")!=kOSBooleanTrue||isInactive())break;
        if(r->operation==DWA_NET_OPEN) {
            if(!liveTX||!liveRX||!wpaVerified||!dataProbeActive||networkBridge->active||sessionExpiredLocked(now))break;
            dwa_net_open(networkBridge,liveIdentity.mac,now);
            setProperty("NetworkSessionActive",true);setProperty("WPA2DataPortAuthorized",true);
        } else {
            if(!networkAliveLocked(now))break;
            if(r->operation==DWA_NET_CLOSE){result=stopLiveLocked()?kIOReturnSuccess:kIOReturnIOError;break;}
            if(r->operation==DWA_NET_PULSE)networkBridge->pulse=now;
            else if(r->operation==DWA_NET_TX) {
                dwa_net_frame frame;
                if(dwa_net_pop(networkBridge,0,&frame,now)){out->length=frame.length;bcopy(frame.data,out->data,frame.length);}
                bzero(&frame,sizeof(frame));
            } else if(!dwa_net_push(networkBridge,1,r->data,r->length,now)) {
                result=kIOReturnNoSpace;break;
            }
        }
        if(r->operation==DWA_NET_OPEN||r->operation==DWA_NET_PULSE) {
            if(associationTimer->setTimeoutMS(DWA_NET_HEARTBEAT_MS)!=kIOReturnSuccess){stopLiveLocked();result=kIOReturnIOError;break;}
        }
        result=kIOReturnSuccess;
    }while(false);
    out->version=DWA_NET_VERSION;out->active=networkAliveLocked(netMS());
    out->tx_dropped=networkBridge->tx.dropped;out->rx_dropped=networkBridge->rx.dropped;
    IOLockUnlock(lifecycleLock);return result;
}
