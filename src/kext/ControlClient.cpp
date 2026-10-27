#include "ControlClient.h"
#include "DWA121Diagnostic.h"
#include "../control/protocol.h"
#include <IOKit/IOLib.h>
#include <kern/task.h>
OSDefineMetaClassAndStructors(DWA121ControlClient,IOUserClient)
bool DWA121ControlClient::initWithTask(task_t task,void *security,UInt32 type)
{
    driver=0; bzero(&method,sizeof(method));
    if (type!=DWA_CONTROL_TYPE || clientHasPrivilege(task,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)
        return false;
    if (!IOUserClient::initWithTask(task,security,type)) return false;
    method.object=this; method.func=(IOMethod)&DWA121ControlClient::command;
    method.flags=kIOUCStructIStructO; method.count0=sizeof(dwa_control_request);
    method.count1=sizeof(dwa_control_reply);
    bzero(&associationMethod,sizeof(associationMethod));
    associationMethod.object=this; associationMethod.func=(IOMethod)&DWA121ControlClient::association;
    associationMethod.flags=kIOUCStructIStructO;
    associationMethod.count0=sizeof(dwa_assoc_request); associationMethod.count1=sizeof(dwa_assoc_reply);
    bzero(&wpaMethod,sizeof(wpaMethod));wpaMethod.object=this;
    wpaMethod.func=(IOMethod)&DWA121ControlClient::wpa;
    wpaMethod.flags=kIOUCStructIStructO;
    wpaMethod.count0=sizeof(dwa_wpa_request);wpaMethod.count1=sizeof(dwa_assoc_reply);
    bzero(&networkMethod,sizeof(networkMethod));networkMethod.object=this;
    networkMethod.func=(IOMethod)&DWA121ControlClient::network;
    networkMethod.flags=kIOUCStructIStructO;
    networkMethod.count0=sizeof(dwa_net_request);networkMethod.count1=sizeof(dwa_net_reply);
    return true;
}
bool DWA121ControlClient::start(IOService *provider)
{
    DWA121Diagnostic *d=OSDynamicCast(DWA121Diagnostic,provider);
    if (!d || !IOUserClient::start(provider)) return false;
    if (d->claimControl(this)!=kIOReturnSuccess) { IOUserClient::stop(provider); return false; }
    driver=d; driver->retain(); return true;
}
IOReturn DWA121ControlClient::clientClose()
{
    if (driver) driver->releaseControl(this);
    terminate(); return kIOReturnSuccess;
}
IOReturn DWA121ControlClient::clientDied() { return clientClose(); }
void DWA121ControlClient::stop(IOService *provider)
{
    if (driver) driver->releaseControl(this);
    IOUserClient::stop(provider);
}
void DWA121ControlClient::free()
{
    if (driver) { driver->releaseControl(this); driver->release(); driver=0; }
    IOUserClient::free();
}
IOExternalMethod *DWA121ControlClient::getTargetAndMethodForIndex(IOService **target,UInt32 index)
{
    if (index>3 || !target) return 0;
    *target=this; return index==3?&networkMethod:(index==2?&wpaMethod:(index?&associationMethod:&method));
}
IOReturn DWA121ControlClient::network(void *input,void *output,IOByteCount inSize,IOByteCount *outSize)
{
    if(!input||!output||!outSize||inSize!=sizeof(dwa_net_request)||*outSize!=sizeof(dwa_net_reply))return kIOReturnBadArgument;
    bzero(output,sizeof(dwa_net_reply));
    IOReturn result=clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator);
    if(result==kIOReturnSuccess)result=driver?driver->networkCommand(this,(const dwa_net_request *)input,(dwa_net_reply *)output):kIOReturnNotAttached;
    bzero(input,inSize);return result;
}
IOReturn DWA121ControlClient::wpa(void *input,void *output,IOByteCount inSize,IOByteCount *outSize)
{
    if(!input||!output||!outSize||inSize!=sizeof(dwa_wpa_request)||*outSize!=sizeof(dwa_assoc_reply))return kIOReturnBadArgument;
    bzero(output,sizeof(dwa_assoc_reply));
    IOReturn result=clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator);
    if(result==kIOReturnSuccess)result=driver?driver->wpaCommand(this,(const dwa_wpa_request *)input,(dwa_assoc_reply *)output):kIOReturnNotAttached;
    /* Mach's copied input may contain a temporal key. Never retain or log it. */
    volatile unsigned char *p=(volatile unsigned char *)input;
    for(unsigned int i=0;i<sizeof(dwa_wpa_request);++i)p[i]=0;
    return result;
}
IOReturn DWA121ControlClient::association(void *input,void *output,IOByteCount inSize,IOByteCount *outSize)
{
    if(!input || !output || !outSize || inSize!=sizeof(dwa_assoc_request) ||
       *outSize!=sizeof(dwa_assoc_reply)) return kIOReturnBadArgument;
    bzero(output,sizeof(dwa_assoc_reply));
    IOReturn privilege=clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator);
    if(privilege!=kIOReturnSuccess) return privilege;
    if(!driver) return kIOReturnNotAttached;
    return driver->associationCommand(this,(const dwa_assoc_request *)input,(dwa_assoc_reply *)output);
}
IOReturn DWA121ControlClient::command(void *input,void *output,IOByteCount inSize,IOByteCount *outSize)
{
    if (!input || !output || !outSize || inSize!=sizeof(dwa_control_request) ||
        *outSize!=sizeof(dwa_control_reply)) return kIOReturnBadArgument;
    bzero(output,sizeof(dwa_control_reply));
    IOReturn privilege=clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator);
    if (privilege!=kIOReturnSuccess) return privilege;
    if (!driver) return kIOReturnNotAttached;
    return driver->controlCommand(this,(const dwa_control_request *)input,(dwa_control_reply *)output);
}
