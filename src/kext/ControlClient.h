#ifndef DWA121_CONTROL_CLIENT_H
#define DWA121_CONTROL_CLIENT_H
#include <IOKit/IOUserClient.h>
class DWA121Diagnostic;
class DWA121ControlClient : public IOUserClient {
    OSDeclareDefaultStructors(DWA121ControlClient)
    DWA121Diagnostic *driver;
    IOExternalMethod method;
    IOExternalMethod associationMethod;
    IOExternalMethod wpaMethod;
    IOExternalMethod networkMethod;
    IOReturn network(void *,void *,IOByteCount,IOByteCount *);
    IOReturn command(void *,void *,IOByteCount,IOByteCount *);
    IOReturn association(void *,void *,IOByteCount,IOByteCount *);
    IOReturn wpa(void *,void *,IOByteCount,IOByteCount *);
protected:
    virtual void free();
public:
    virtual bool initWithTask(task_t,void *,UInt32);
    virtual bool start(IOService *);
    virtual void stop(IOService *);
    virtual IOReturn clientClose();
    virtual IOReturn clientDied();
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **,UInt32);
};
#endif
