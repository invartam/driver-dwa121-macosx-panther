/* Panther supervisor. Root-only fcntl lock: no PID supplied by an untrusted
 * caller, no socket, no credentials, no automatic network association. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define STATE "/var/db/dwa121-airport"
#define BIN "/usr/local/libexec/dwa121-airport/0.3.4/bin/"
static volatile sig_atomic_t stopping;
static int lockfd;
static void stop(int sig){(void)sig;stopping=1;}
static int owner(void)
{
    struct flock l;memset(&l,0,sizeof(l));l.l_type=F_WRLCK;l.l_whence=SEEK_SET;
    if(fcntl(lockfd,F_GETLK,&l)<0)return -1;
    return l.l_type==F_UNLCK?0:l.l_pid;
}
static pid_t launch(int broker)
{
    pid_t pid=fork();if(pid)return pid;
    close(lockfd);
    if(broker)execl(BIN "dwa121-airport-broker",BIN "dwa121-airport-broker",
        "--connect",BIN "dwa121-connect",(char *)0);
    else execl(BIN "dwa121-airport-monitor",BIN "dwa121-airport-monitor","--watch",(char *)0);
    _exit(127);
}
static int supervise(int ready)
{
    struct flock l;struct sigaction sa;pid_t broker=0,monitor=0;int fd,i;char response='0';
    memset(&l,0,sizeof(l));l.l_type=F_WRLCK;l.l_whence=SEEK_SET;
    if(fcntl(lockfd,F_SETLK,&l)<0)goto done;
    memset(&sa,0,sizeof(sa));sa.sa_handler=stop;sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,&sa,0);sigaction(SIGINT,&sa,0);sigaction(SIGHUP,&sa,0);
    rename(STATE "/runtime.log",STATE "/runtime.previous.log");
    fd=open(STATE "/runtime.log",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0)goto done;
    dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);
    broker=launch(1);if(broker<0){broker=0;goto done;}
    monitor=launch(0);if(monitor<0){monitor=0;goto done;}
    /* Report ready only if both exec paths survive their initial checks. */
    for(i=0;i<10&&!stopping;++i){
        if(waitpid(broker,0,WNOHANG)==broker){broker=0;goto done;}
        if(waitpid(monitor,0,WNOHANG)==monitor){monitor=0;goto done;}
        usleep(100000);
    }
    if(stopping)goto done;
    response='1';(void)write(ready,&response,1);close(ready);ready=-1;
    while(!stopping){
        if(waitpid(broker,0,WNOHANG)==broker){broker=0;break;}
        if(waitpid(monitor,0,WNOHANG)==monitor){monitor=0;break;}
        usleep(100000);
    }
done:
    if(ready>=0){(void)write(ready,&response,1);close(ready);}
    if(broker>0)kill(broker,SIGTERM);if(monitor>0)kill(monitor,SIGTERM);
    /* Broker owns and reaps its supplicant. Never kill an unrelated PID or
     * force a kext unload. If cleanup stalls, the manager reports failure. */
    if(broker>0)while(waitpid(broker,0,0)<0&&errno==EINTR){}
    if(monitor>0)while(waitpid(monitor,0,0)<0&&errno==EINTR){}
    close(lockfd);return response=='1'?0:1;
}
int main(int argc,char **argv)
{
    struct stat st;struct rlimit limit;int pid,i,pipes[2],fd;char response=0;
    umask(077);limit.rlim_cur=limit.rlim_max=0;
    if(argc!=2||geteuid()!=0||setrlimit(RLIMIT_CORE,&limit))return 1;
    if(lstat(STATE,&st)||!S_ISDIR(st.st_mode)||st.st_uid||(st.st_mode&077))return 1;
    lockfd=open(STATE "/runtime.lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(lockfd<0||fstat(lockfd,&st)||!S_ISREG(st.st_mode)||st.st_uid||(st.st_mode&077)||st.st_nlink!=1)return 1;
    pid=owner();if(pid<0)return 1;
    if(!strcmp(argv[1],"status")){puts(pid?"AirPort runtime: running":"AirPort runtime: stopped");return pid?0:1;}
    if(!strcmp(argv[1],"stop")){
        if(!pid)return 0;if(kill(pid,SIGTERM))return 1;
        for(i=0;i<200;++i){if(!owner())return 0;usleep(100000);}
        fputs("Runtime still stopping; no forced termination.\n",stderr);return 1;
    }
    if(strcmp(argv[1],"start"))return 1;if(pid)return 0;
    if(pipe(pipes))return 1;pid=fork();if(pid<0)return 1;
    if(!pid){close(pipes[0]);setsid();chdir("/");
        fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);}
        return supervise(pipes[1]);}
    close(pipes[1]);close(lockfd);
    do{i=read(pipes[0],&response,1);}while(i<0&&errno==EINTR);
    close(pipes[0]);return i==1&&response=='1'?0:1;
}
