/* Foreground, root-only coordinator. Secrets travel once through IOKit and an
 * anonymous pipe; never argv, environment, a file, preferences or the log. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "../control/airport_protocol.h"
static volatile sig_atomic_t stopping;
static io_connect_t connection;
static void stop(int sig){(void)sig;stopping=1;}
static void wipe(void *v,unsigned int n){volatile unsigned char *p=v;while(n--)*p++=0;}
static int protectedExecutable(const char *path,char resolved[PATH_MAX])
{
    struct stat st;char *p;
    if(!realpath(path,resolved))return 0;
    for(p=resolved+1;*p;++p)if(*p=='/'){
        int ok;*p=0;ok=!lstat(resolved,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&022);*p='/';
        if(!ok)return 0;
    }
    return !lstat(resolved,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&!(st.st_mode&022)&&(st.st_mode&0111);
}
static kern_return_t call(unsigned int op,unsigned int generation,struct dwa_airport_reply *out)
{
    struct dwa_airport_request in;IOByteCount n=sizeof(*out);kern_return_t r;
    memset(&in,0,sizeof(in));memset(out,0,sizeof(*out));
    in.version=DWA_AIRPORT_VERSION;in.operation=op;in.generation=generation;
    r=IOConnectMethodStructureIStructureO(connection,0,sizeof(in),&n,&in,out);
    if(!r&&n!=sizeof(*out)){wipe(out,sizeof(*out));return kIOReturnBadArgument;}
    return r;
}
static pid_t launch(const char *binary,struct dwa_airport_reply *r)
{
    int pipes[2];pid_t child;unsigned int i,done=0;char generation[16],ssid[65];
    const char *hex="0123456789abcdef";
    if(r->secret_length<8||r->secret_length>63||!r->target.ssid_length||r->target.ssid_length>31)return -1;
    snprintf(generation,sizeof(generation),"%lu",(unsigned long)r->target.generation);
    for(i=0;i<r->target.ssid_length;++i){ssid[2*i]=hex[r->target.ssid[i]>>4];ssid[2*i+1]=hex[r->target.ssid[i]&15];}
    ssid[2*i]=0;if(pipe(pipes))return -1;
    child=fork();
    if(!child){
        close(pipes[1]);if(dup2(pipes[0],STDIN_FILENO)<0)_exit(127);close(pipes[0]);
        /* Do not close the inherited Mach user-client port: parent owns it.
         * exec destroys the child's inherited task ports and memory. */
        wipe(r,sizeof(*r));execl(binary,binary,"--airport",generation,ssid,(char *)0);_exit(127);
    }
    close(pipes[0]);
    if(child>0){
        while(done<r->secret_length){ssize_t n=write(pipes[1],r->secret+done,r->secret_length-done);
            if(n<0&&errno==EINTR)continue;if(n<=0)break;done+=(unsigned int)n;}
        if(done==r->secret_length)(void)write(pipes[1],"\n",1);
    }
    close(pipes[1]);wipe(r->secret,sizeof(r->secret));r->secret_length=0;
    return child;
}
int main(int argc,char **argv)
{
    struct rlimit limit;struct sigaction sa;io_service_t service;char executable[PATH_MAX];
    struct dwa_airport_reply reply;pid_t child=0;unsigned int generation=0,oldPhase=~0U;
    int result=0,recover=0;
    if(argc!=3||strcmp(argv[1],"--connect")||argv[2][0]!='/'||geteuid()!=0){
        fprintf(stderr,"Usage (root): dwa121-airport-broker --connect /absolute/path/dwa121-connect\n");return 1;
    }
    if(!protectedExecutable(argv[2],executable)){
        fprintf(stderr,"Connection executable and parent directories must be root-owned and protected\n");return 1;
    }
    limit.rlim_cur=limit.rlim_max=0;if(setrlimit(RLIMIT_CORE,&limit))return 1;
    memset(&sa,0,sizeof(sa));sa.sa_handler=stop;sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,&sa,0);sigaction(SIGINT,&sa,0);sigaction(SIGHUP,&sa,0);signal(SIGPIPE,SIG_IGN);
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AirPortDWA121"));
    if(!service)return 1;
    result=IOServiceOpen(service,mach_task_self(),DWA_AIRPORT_BROKER_TYPE,&connection);IOObjectRelease(service);
    if(result){fprintf(stderr,"Exclusive AirPort broker unavailable: %08x\n",result);return 1;}
    result=0;puts("AirPort broker ready: native password required; no saved-profile fallback");fflush(stdout);
    while(!stopping){
        kern_return_t status=call(DWA_AP_POLL,0,&reply);
        if(status==kIOReturnBusy){usleep(100000);continue;}
        if(status){fprintf(stderr,"AirPort provider unavailable: %08x\n",status);result=1;break;}
        if(reply.target.phase!=oldPhase){
            printf("AirPort session=%lu phase=%lu\n",(unsigned long)reply.target.generation,(unsigned long)reply.target.phase);fflush(stdout);oldPhase=reply.target.phase;
        }
        if(child){int code;pid_t ended=waitpid(child,&code,WNOHANG);
            if(ended==child){printf("AirPort session=%u process ended status=%d\n",generation,code);fflush(stdout);child=0;recover=1;}
            else if(reply.target.phase==DWA_AP_DISCONNECTED||reply.target.phase==DWA_AP_FAILED)kill(child,SIGTERM);
        }
        if(recover&&!child){
            status=call(DWA_AP_CANCEL,generation,&reply);
            if(!status){usleep(300000);status=call(DWA_AP_RECOVER,0,&reply);}
            if(!status){recover=0;puts("AirPort radio ready for a new scan");fflush(stdout);}
            else if(status!=kIOReturnBusy){fprintf(stderr,"Radio recovery failed: %08x\n",status);result=1;break;}
        }else if(!child&&reply.target.phase==DWA_AP_REQUESTED){
            generation=reply.target.generation;status=call(DWA_AP_TAKE,generation,&reply);
            if(!status){child=launch(executable,&reply);if(child<0){child=0;recover=1;}}
            else if(status!=kIOReturnBusy){fprintf(stderr,"Native request refused: %08x\n",status);recover=1;}
        }
        wipe(&reply,sizeof(reply));usleep(100000);
    }
    wipe(&reply,sizeof(reply));
    if(child){unsigned int i;kill(child,SIGTERM);
        for(i=0;i<50;++i){if(waitpid(child,0,WNOHANG)==child){child=0;break;}usleep(100000);}
        if(child){kill(child,SIGKILL);waitpid(child,0,0);}
    }
    IOServiceClose(connection);return result;
}
