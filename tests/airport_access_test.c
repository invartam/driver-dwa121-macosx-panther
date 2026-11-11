/* Panther integration test. Public fixture only; run with radio disconnected.
 * allow: console account/root; deny: another UID (e.g. nobody).
 * hold: keep a reference across logout/login, then expect revocation. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <servers/bootstrap.h>
#include <mach/mach.h>
#include <sys/wait.h>
#include "../src/control/airport_protocol.h"
#include "../src/control/protocol.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static io_connect_t client;
static int privateKind;
static kern_return_t call(unsigned char *in,unsigned int n,void *out,unsigned int bytes)
{ IOByteCount size=bytes;unsigned char unused=0;
  return IOConnectMethodStructureIStructureO(client,0,n,&size,in,out?out:&unused); }
static void mutations(int allowed)
{
    unsigned char stage[]={26,'p','0','3','4','t','e','s','t',0};
    unsigned char disconnect[57],scan[34],power[5];kern_return_t r;
    if(privateKind==1){
        struct dwa_airport_request in;struct dwa_airport_reply out;
        memset(&in,0,sizeof(in));in.version=DWA_AIRPORT_VERSION;in.operation=DWA_AP_POLL;
        r=call((unsigned char *)&in,sizeof(in),&out,sizeof(out));
        CHECK(r==(allowed?KERN_SUCCESS:kIOReturnNotPrivileged));return;
    }
    if(privateKind==2){
        struct dwa_control_request in;struct dwa_control_reply out;
        memset(&in,0,sizeof(in));in.version=DWA_CONTROL_VERSION;in.operation=DWA_CONTROL_STATUS;
        r=call((unsigned char *)&in,sizeof(in),&out,sizeof(out));
        CHECK(r==(allowed?KERN_SUCCESS:kIOReturnNotPrivileged));return;
    }
    memset(disconnect,0,sizeof(disconnect));disconnect[0]=17;disconnect[22]=1;
    memset(disconnect+23,255,4);memset(scan,0,sizeof(scan));scan[0]=8;
    memset(power,0,sizeof(power));power[0]=12;
    r=call(stage,sizeof(stage),0,0);CHECK(r==(allowed?KERN_SUCCESS:kIOReturnNotPrivileged));
    r=call(disconnect,sizeof(disconnect),0,0);CHECK(r==(allowed?KERN_SUCCESS:kIOReturnNotPrivileged));
    r=call(power,sizeof(power),0,0);CHECK(r==(allowed?kIOReturnUnsupported:kIOReturnNotPrivileged));
    r=call(scan,sizeof(scan),0,0);CHECK(allowed?r!=kIOReturnNotPrivileged:r==kIOReturnNotPrivileged);
    /* Malformed mutations cannot bypass the same gate. */
    disconnect[0]=17;r=call(disconnect,2,0,0);
    CHECK(r==(allowed?kIOReturnBadArgument:kIOReturnNotPrivileged));
}
static void transferred(void)
{
    mach_port_t bootstrap,channel;char name[128];pid_t child;int status;
    struct request {mach_msg_header_t h;mach_msg_trailer_t trailer;} request;
    struct reply {mach_msg_header_t h;mach_msg_body_t body;
        mach_msg_port_descriptor_t port;mach_msg_trailer_t trailer;} reply;
    CHECK(!task_get_bootstrap_port(mach_task_self(),&bootstrap));
    CHECK(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&channel));
    CHECK(!mach_port_insert_right(mach_task_self(),channel,channel,MACH_MSG_TYPE_MAKE_SEND));
    snprintf(name,sizeof(name),"local.dwa121.access-test.%lu",(unsigned long)getpid());
    CHECK(!bootstrap_register(bootstrap,name,channel));
    child=fork();CHECK(child>=0);
    if(!child){
        mach_port_t destination,answer;unsigned char op=18,out[56];
        CHECK(!task_get_bootstrap_port(mach_task_self(),&bootstrap));
        CHECK(!bootstrap_look_up(bootstrap,name,&destination));
        CHECK(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&answer));
        memset(&request,0,sizeof(request));request.h.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,MACH_MSG_TYPE_MAKE_SEND_ONCE);
        request.h.msgh_remote_port=destination;request.h.msgh_local_port=answer;
        request.h.msgh_size=sizeof(mach_msg_header_t);
        CHECK(!mach_msg(&request.h,MACH_SEND_MSG,request.h.msgh_size,0,MACH_PORT_NULL,0,MACH_PORT_NULL));
        memset(&reply,0,sizeof(reply));
        CHECK(!mach_msg(&reply.h,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(reply),answer,5000,MACH_PORT_NULL));
        CHECK(reply.body.msgh_descriptor_count==1);client=reply.port.name;
        if(!privateKind)CHECK(!call(&op,1,out,sizeof(out)));mutations(0);
        /* Panther's io_service_close discards clientClose's return value.
         * The owner's subsequent calls prove that closure had no effect. */
        CHECK(IOServiceClose(client)==KERN_SUCCESS);
        _exit(0);
    }
    memset(&request,0,sizeof(request));
    CHECK(!mach_msg(&request.h,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(request),channel,5000,MACH_PORT_NULL));
    memset(&reply,0,sizeof(reply));reply.h.msgh_bits=MACH_MSGH_BITS_COMPLEX|MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE,0);
    reply.h.msgh_remote_port=request.h.msgh_remote_port;
    reply.h.msgh_size=sizeof(reply)-sizeof(reply.trailer);reply.body.msgh_descriptor_count=1;
    reply.port.name=client;reply.port.disposition=MACH_MSG_TYPE_COPY_SEND;reply.port.type=MACH_MSG_PORT_DESCRIPTOR;
    CHECK(!mach_msg(&reply.h,MACH_SEND_MSG,reply.h.msgh_size,0,MACH_PORT_NULL,0,MACH_PORT_NULL));
    CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
    /* Panther may refuse deregistration. Destroy the receive right in all
     * cases: any surviving bootstrap name refers only to a dead endpoint. */
    (void)bootstrap_register(bootstrap,name,MACH_PORT_NULL);
    mach_port_destroy(mach_task_self(),channel);mach_port_deallocate(mach_task_self(),bootstrap);
    mutations(1);puts("PASS: transferred port RPC/close policy; owner remains usable");
}
int main(int argc,char **argv)
{
    io_service_t service;unsigned char op=18,status[56];int allowed,hold,live;
    CHECK(argc==2);
    live=!strcmp(argv[1],"deny-live");
    if(!strcmp(argv[1],"broker-transfer"))privateKind=1;
    if(!strcmp(argv[1],"core-transfer"))privateKind=2;
    if(!strcmp(argv[1],"republish")||!strcmp(argv[1],"reject-publish")){
        io_registry_entry_t root=IORegistryGetRootEntry(kIOMasterPortDefault);
        CFTypeRef users=IORegistryEntryCreateCFProperty(root,CFSTR("IOConsoleUsers"),0,0);
        CFMutableDictionaryRef properties=CFDictionaryCreateMutable(0,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        kern_return_t r;CHECK(root&&users&&properties);
        CFDictionarySetValue(properties,CFSTR("IOConsoleUsers"),users);
        service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("IOResources"));CHECK(service);
        r=IORegistryEntrySetCFProperties(service,properties);
        CHECK(!strcmp(argv[1],"republish")?r==KERN_SUCCESS:r==kIOReturnNotPrivileged);
        CFRelease(properties);CFRelease(users);IOObjectRelease(root);IOObjectRelease(service);
        puts("PASS: console publication privilege");return 0;
    }
    hold=!strcmp(argv[1],"hold");allowed=hold||privateKind||!strcmp(argv[1],"allow")||!strcmp(argv[1],"transfer");
    CHECK(allowed||live||!strcmp(argv[1],"deny"));
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching(privateKind==2?"DWA121Diagnostic":"AirPortDWA121"));CHECK(service);
    CHECK(!IOServiceOpen(service,mach_task_self(),privateKind==2?DWA_CONTROL_TYPE:privateKind==1?DWA_AIRPORT_BROKER_TYPE:12,&client));IOObjectRelease(service);
    if(!privateKind){CHECK(!call(&op,1,status,sizeof(status)));
        CHECK(live?status[55]==1:status[55]!=1);} /* Only unauthorized mutations on a live link. */
    mutations(allowed);
    if(live){unsigned char after[56];CHECK(!call(&op,1,after,sizeof(after)));
        CHECK(!memcmp(after,status,sizeof(status)));puts("PASS: unauthorized mutations preserve live identity/state");}
    if(privateKind||!strcmp(argv[1],"transfer"))transferred();
    if(hold){puts("READY: reference retained; change console session then send newline");fflush(stdout);
        CHECK(getchar()!=EOF);mutations(0);}
    IOServiceClose(client);
    printf("PASS: uid=%lu euid=%lu reads allowed, mutations %s\n",
        (unsigned long)getuid(),(unsigned long)geteuid(),hold?"revoked":allowed?"allowed":"denied");
    return 0;
}
