/* Panther has no launchd. A small, root-only supervisor owns the fcntl lock;
 * stop obtains the PID from the kernel, never from user-writable pid text. */
#include "../config/config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define BASE "/Library/Application Support/DWA121"
#define BIN "/usr/local/libexec/dwa121/"
static volatile sig_atomic_t stopping;
static int lockfd=-1;
static void stop_handler(int sig) { (void)sig;stopping=1; }
static int locked_pid(void)
{
    struct flock lock;memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fcntl(lockfd,F_GETLK,&lock)<0)return -1;
    return lock.l_type==F_UNLCK?0:(int)lock.l_pid;
}
static int run(const char *path,const char *a,const char *b,int interruptible)
{
    pid_t pid;int code=0;pid=fork();if(pid<0)return -1;
    if(!pid){close(lockfd);execl(path,path,a,b,(char *)0);_exit(127);}
    for(;;){
        pid_t got=waitpid(pid,&code,WNOHANG);
        if(got==pid)break;
        if(got<0&&errno!=EINTR)return -1;
        if(stopping&&interruptible)kill(pid,SIGTERM);
        usleep(100000);
    }
    return WIFEXITED(code)?WEXITSTATUS(code):-1;
}
static int supervise(int ready)
{
    struct flock lock;struct sigaction action;int fd,delay,code;char response='0';struct dwa_config config;
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fcntl(lockfd,F_SETLK,&lock)<0||!dwa_config_read(DWA_CONFIG_PATH,&config))goto failed;
    dwa_config_clear(&config,sizeof(config));
    memset(&action,0,sizeof(action));action.sa_handler=stop_handler;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,0);sigaction(SIGINT,&action,0);sigaction(SIGHUP,&action,0);
    response='1';(void)write(ready,&response,1);close(ready);
    while(!stopping){
        rename(BASE "/session.log",BASE "/session.previous.log");
        fd=open(BASE "/session.log",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
        if(fd<0)break;
        dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);if(fd>2)close(fd);
        printf("DWA121: starting a fresh WPA2 session\n");fflush(stdout);
        /* prepare refuses ANY loaded DWA121 module, preserving other tests. */
        code=run(BIN "prepare.sh",0,0,0);
        if(code){fprintf(stderr,"Preparation failed (%d); service stopped.\n",code);break;}
        code=run("/sbin/kextload",BASE "/DWA121Diagnostic.kext",0,0);
        if(!code&&!stopping)code=run(BIN "dwa121-connect","--config",DWA_CONFIG_PATH,1);
        fprintf(stderr,"Session ended (%d); unloading USB driver.\n",code);
        if(run(BIN "unload.sh",0,0,0)){
            fprintf(stderr,"Cannot unload driver; automatic retries stopped.\n");break;
        }
        for(delay=0;delay<50&&!stopping;++delay)usleep(100000);
    }
    close(lockfd);return 0;
failed:
    (void)write(ready,&response,1);close(ready);return 1;
}
int main(int argc,char **argv)
{
    struct stat st;struct rlimit limit;int pid,i,pipes[2],fd;char ready=0;
    limit.rlim_cur=limit.rlim_max=0;umask(077);
    if(argc!=2||geteuid()!=0||setrlimit(RLIMIT_CORE,&limit))return 1;
    if(lstat(BASE,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=0||(st.st_mode&077)!=0){fprintf(stderr,"DWA121 directory must be root owned, mode 0700.\n");return 1;}
    if(lstat(BASE "/service.lock",&st)==0&&!S_ISREG(st.st_mode))return 1;
    lockfd=open(BASE "/service.lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(lockfd<0||fstat(lockfd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&077)!=0||st.st_nlink!=1)return 1;
    pid=locked_pid();if(pid<0)return 1;
    if(!strcmp(argv[1],"status")){printf("DWA121 service: %s\n",pid?"running":"stopped");return pid?0:1;}
    if(!strcmp(argv[1],"stop")){
        if(!pid)return 0;
        if(kill(pid,SIGTERM))return 1;
        for(i=0;i<300;++i){if(locked_pid()==0){puts("DWA121 stopped.");return 0;}usleep(100000);}
        fprintf(stderr,"DWA121 still stopping; no forced termination performed.\n");return 1;
    }
    if(strcmp(argv[1],"start")){fprintf(stderr,"Usage: dwa121-service start|stop|status\n");return 1;}
    if(pid){puts("DWA121 already running.");return 0;}
    if(pipe(pipes))return 1;
    pid=fork();if(pid<0)return 1;
    if(!pid){
        close(pipes[0]);setsid();chdir("/");
        fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);}
        return supervise(pipes[1]);
    }
    close(pipes[1]);close(lockfd);
    do{i=read(pipes[0],&ready,1);}while(i<0&&errno==EINTR);
    close(pipes[0]);
    puts(i==1&&ready=='1'?"DWA121 started. See session.log for connection status.":"DWA121 could not start. Check configuration and permissions.");
    return i==1&&ready=='1'?0:1;
}
