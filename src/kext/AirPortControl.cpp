#include "DWA121Diagnostic.h"
#include "PersistentRX.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/c++/OSBoolean.h>
#include <kern/clock.h>
static rtl_u32 airportMS()
{
    AbsoluteTime t;UInt64 ns;clock_get_uptime(&t);
    absolutetime_to_nanoseconds(t,&ns);return (rtl_u32)(ns/1000000ULL);
}
IOReturn DWA121Diagnostic::airportTarget(DWA121ControlClient *client,dwa_airport_target *out)
{
    IOLockLock(lifecycleLock);
    IOReturn r=kIOReturnNotReady;
    if(client==controlOwner&&airportReserved&&airportTargetState.phase==DWA_AP_RUNNING){
        *out=airportTargetState;r=kIOReturnSuccess;
    }
    IOLockUnlock(lifecycleLock);return r;
}
IOReturn DWA121Diagnostic::airportPlatform(const OSSymbol *name,void *in,void *out)
{
    bool reserve=name->isEqualTo(DWA_AIRPORT_RESERVE),status=name->isEqualTo(DWA_AIRPORT_STATUS);
    bool prepare=name->isEqualTo(DWA_AIRPORT_PREPARE),cancel=name->isEqualTo(DWA_AIRPORT_CANCEL);
    bool recover=name->isEqualTo(DWA_AIRPORT_RECOVER);
    if(!out||(!status&&!recover&&!in))return kIOReturnBadArgument;
    /* Native selection/status never waits behind calibration or USB drain. */
    if(!IOLockTryLock(lifecycleLock))return kIOReturnBusy;
    IOReturn result=kIOReturnNotReady;
    do {
        if(usbTerminating||isInactive())break;
        if(status){
            if(airportReserved&&networkAliveLocked(airportMS()))airportTargetState.phase=DWA_AP_CONNECTED;
            else if(airportTargetState.phase==DWA_AP_CONNECTED)airportTargetState.phase=DWA_AP_FAILED;
            result=kIOReturnSuccess;break;
        }
        if(recover){
            if(controlOwner||liveTX||airportReserved||scanActive){result=kIOReturnBusy;break;}
            if(liveRX){result=kIOReturnSuccess;break;}
            if(getProperty("PersistentStopVerified")!=kOSBooleanTrue)break;
            result=startHardwareLocked(getProvider(),true)?kIOReturnSuccess:kIOReturnIOError;
            break;
        }
        if(reserve){
            const dwa_join_selection *request=(const dwa_join_selection *)in;
            if(request->operation!=DWA_JOIN_SELECT||request->legacy_prompt!=2){result=kIOReturnUnsupported;break;}
            if(controlOwner||liveTX||airportReserved||scanActive){result=kIOReturnBusy;break;}
            if(!liveRX||!scanValid)break;
            /* Scratch is heap allocated and protected by the lifecycle lock. */
            rtl_bss *chosen=&scanScratch->entries[0];
            if(!dwa_join_choose(scanTable,request->ssid,request->ssid_length,liveChannel,airportMS(),chosen))break;
            bzero(&airportTargetState,sizeof(airportTargetState));
            airportTargetState.version=DWA_AIRPORT_VERSION;
            if(++airportSequence==0)++airportSequence;
            airportTargetState.generation=airportSequence;airportTargetState.phase=DWA_AP_REQUESTED;
            airportTargetState.channel=chosen->channel;airportTargetState.ssid_length=chosen->ssid_length;
            bcopy(chosen->ssid,airportTargetState.ssid,chosen->ssid_length);
            bcopy(chosen->bssid,airportTargetState.bssid,6);airportReserved=true;
            result=associationTimer->setTimeoutMS(15000);
            if(result!=kIOReturnSuccess){airportReserved=false;airportTargetState.phase=DWA_AP_FAILED;}
            IOLog("DWA121 AirPort: session=%lu requested channel=%lu BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n",
                (unsigned long)airportSequence,(unsigned long)airportTargetState.channel,
                chosen->bssid[0],chosen->bssid[1],chosen->bssid[2],chosen->bssid[3],chosen->bssid[4],chosen->bssid[5]);
            break;
        }
        if(*(rtl_u32 *)in!=airportTargetState.generation){result=kIOReturnNotReady;break;}
        if(cancel){
            if(airportReserved){
                if(liveTX){if(!stopLiveLocked()){result=kIOReturnIOError;break;}}
                else {if(liveRX)liveRX->setCapture(false);controlOwner=0;airportReserved=false;associationTimer->cancelTimeout();}
            }
            airportTargetState.phase=DWA_AP_DISCONNECTED;result=kIOReturnSuccess;break;
        }
        if(prepare){
            if(!airportReserved||airportTargetState.phase!=DWA_AP_REQUESTED||controlOwner||liveTX||!liveRX)break;
            if(liveChannel!=airportTargetState.channel&&!scanTuneLocked(airportTargetState.channel)){
                stopLiveLocked();result=kIOReturnIOError;break;
            }
            airportTargetState.phase=DWA_AP_PREPARED;result=kIOReturnSuccess;
        }
    }while(false);
    *(dwa_airport_target *)out=airportTargetState;
    IOLockUnlock(lifecycleLock);return result;
}
