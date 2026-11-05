/* Panther hardware transport checks. Uses ONLY a public fixture; never sends
 * auth/EAPOL frames. The temporary broker connection takes/cancels requests. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../src/control/airport_protocol.h"
#include "../src/control/protocol.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static io_connect_t native,broker;
static kern_return_t raw(const void *p,unsigned int n,void *out,unsigned int bytes)
{IOByteCount size=bytes;unsigned char unused=0;return IOConnectMethodStructureIStructureO(native,0,n,&size,(void *)p,out?out:&unused);}
static kern_return_t admin(unsigned int op,unsigned int gen,struct dwa_airport_reply *out)
{
    struct dwa_airport_request in;IOByteCount n=sizeof(*out);
    memset(&in,0,sizeof(in));memset(out,0,sizeof(*out));in.version=DWA_AIRPORT_VERSION;in.operation=op;in.generation=gen;
    return IOConnectMethodStructureIStructureO(broker,0,sizeof(in),&n,&in,out);
}
static void scan(void)
{
    unsigned char start[34],next[5],out[50];unsigned int i;kern_return_t r;
    memset(start,0,sizeof(start));start[0]=8;CHECK(!raw(start,sizeof(start),0,0));
    memset(next,0,sizeof(next));next[0]=9;
    for(i=0;i<200;++i){r=raw(next,sizeof(next),out,sizeof(out));if(r!=0x20000108)break;usleep(50000);}
    CHECK(r==0||r==0x20000109);
}
static void credentials(void)
{unsigned char p[]={26,'p','0','3','2','t','e','s','t',0};CHECK(!raw(p,sizeof(p),0,0));}
static void join(unsigned char p[57],const char *ssid)
{memset(p,0,57);p[0]=17;p[2]=1;p[4]=5;p[22]=3;memcpy(p+23,ssid,strlen(ssid));}
struct join_task {unsigned char wire[57];kern_return_t result;};
static void *join_thread(void *v)
{struct join_task *task=v;task->result=raw(task->wire,57,0,0);return 0;}
static int zero(const void *v,unsigned int n)
{const unsigned char *p=v;unsigned int i;for(i=0;i<n;++i)if(p[i])return 0;return 1;}
int main(int argc,char **argv)
{
    io_service_t service;io_connect_t second=0,owner=0;unsigned char p[57],bad[66],start[34];
    pthread_t thread;struct join_task task;unsigned int tries;
    struct dwa_airport_reply r;unsigned int gen;unsigned char secretReply[64];
    CHECK((argc==2||argc==3)&&strlen(argv[1])>0&&strlen(argv[1])<32);
    CHECK(sizeof(struct dwa_airport_target)==60&&sizeof(struct dwa_airport_reply)==128);
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));CHECK(service);
    CHECK(!IOServiceOpen(service,mach_task_self(),12,&native));
    if(argc==3){
        unsigned char status[56],get=18;unsigned int i;
        CHECK(!strcmp(argv[2],"--active-disconnect"));
        CHECK(!raw(&get,1,status,56)&&status[55]==1&&!strcmp((char *)status+6,argv[1]));
        credentials();join(p,argv[1]);CHECK(raw(p,57,0,0)!=KERN_SUCCESS);
        CHECK(!raw(&get,1,status,56)&&status[55]==1); /* concurrent request preserves connection */
        memset(start,0,sizeof(start));start[0]=8;CHECK(raw(start,sizeof(start),0,0)!=KERN_SUCCESS);
        memset(p,0,sizeof(p));p[0]=17;p[22]=1;memset(p+23,255,4);
        CHECK(!raw(p,57,0,0));
        for(i=0;i<200;++i){if(!raw(&get,1,status,56)&&status[55]==4)break;usleep(50000);}
        CHECK(i<200&&zero(status+6,34)&&zero(status+46,6));
        IOServiceClose(native);IOObjectRelease(service);
        puts("PASS: connected status, concurrent join/scan refused without link loss, native disconnect clears identity");return 0;
    }
    if(geteuid()!=0){CHECK(IOServiceOpen(service,mach_task_self(),DWA_AIRPORT_BROKER_TYPE,&broker)!=KERN_SUCCESS);
        IOServiceClose(native);IOObjectRelease(service);puts("PASS: non-root broker denied");return 0;}
    CHECK(!IOServiceOpen(service,mach_task_self(),DWA_AIRPORT_BROKER_TYPE,&broker));
    CHECK(IOServiceOpen(service,mach_task_self(),DWA_AIRPORT_BROKER_TYPE,&second)!=KERN_SUCCESS);
    IOObjectRelease(service);
    memset(bad,'x',sizeof(bad));bad[0]=26;bad[65]=0;CHECK(raw(bad,sizeof(bad),0,0)!=KERN_SUCCESS);
    bad[9]=0;bad[3]=0;CHECK(raw(bad,10,0,0)!=KERN_SUCCESS);
    join(p,argv[1]);CHECK(raw(p,56,0,0)!=KERN_SUCCESS);CHECK(raw(p,57,0,0)!=KERN_SUCCESS);
    credentials();sleep(11);CHECK(admin(DWA_AP_POLL,0,&r)!=KERN_SUCCESS); /* broker lease expired */
    /* A broker that stops polling is intentionally revoked. */
    IOServiceClose(broker);broker=0;
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));CHECK(service);
    CHECK(!IOServiceOpen(service,mach_task_self(),DWA_AIRPORT_BROKER_TYPE,&broker));IOObjectRelease(service);
    CHECK(raw(p,57,0,0)!=KERN_SUCCESS); /* staged secret expired */
    scan();credentials();join(p,"P032-unknown");CHECK(raw(p,57,0,0)!=KERN_SUCCESS);
    join(p,argv[1]);CHECK(raw(p,57,0,0)!=KERN_SUCCESS); /* unknown consumed credential */
    scan();credentials();memcpy(task.wire,p,57);task.result=kIOReturnError;
    CHECK(!pthread_create(&thread,0,join_thread,&task));
    for(tries=0;tries<100;++tries){CHECK(!admin(DWA_AP_POLL,0,&r));if(r.target.phase==DWA_AP_REQUESTED)break;usleep(20000);}
    CHECK(r.target.phase==DWA_AP_REQUESTED&&zero(r.secret,sizeof(r.secret))&&!r.secret_length);
    gen=r.target.generation;
    memset(start,0,sizeof(start));start[0]=8;CHECK(raw(start,sizeof(start),0,0)!=KERN_SUCCESS);
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("DWA121Diagnostic"));CHECK(service);
    CHECK(IOServiceOpen(service,mach_task_self(),DWA_CONTROL_TYPE,&owner)!=KERN_SUCCESS);IOObjectRelease(service);
    CHECK(admin(DWA_AP_TAKE,gen+1,&r)!=KERN_SUCCESS&&zero(r.secret,sizeof(r.secret)));
    CHECK(!admin(DWA_AP_TAKE,gen,&r)&&r.secret_length==8&&!memcmp(r.secret,"p032test",8));
    memcpy(secretReply,r.secret,64);memset(&r,0,sizeof(r));memset(secretReply,0,sizeof(secretReply));
    CHECK(admin(DWA_AP_TAKE,gen,&r)!=KERN_SUCCESS&&zero(r.secret,sizeof(r.secret)));
    CHECK(!admin(DWA_AP_CANCEL,gen,&r));usleep(400000);
    CHECK(!admin(DWA_AP_POLL,0,&r)&&r.target.phase==DWA_AP_DISCONNECTED);
    CHECK(!pthread_join(thread,0)&&task.result==KERN_SUCCESS);
    scan();IOServiceClose(broker);IOServiceClose(native);
    puts("PASS: broker privilege/exclusion, credential bounds/expiry/consumption, unknown SSID, scan/control exclusion, generation, one-shot secret, cancellation and rescan");return 0;
}
