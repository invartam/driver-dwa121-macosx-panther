#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
void dwa_config_clear(void *p,unsigned int n)
{ volatile unsigned char *v=p;while(n--)*v++=0; }
int dwa_config_valid(const struct dwa_config *c)
{
    unsigned int i;
    if(memcmp(c->magic,"DWA121\001",8)||c->channel<1||c->channel>11||!c->ssid_length||c->ssid_length>32||c->reserved[0]||c->reserved[1])return 0;
    for(i=0;i<c->ssid_length;++i)if(!c->ssid[i])return 0;
    for(i=c->ssid_length;i<32;++i)if(c->ssid[i])return 0;
    return 1;
}
int dwa_config_read(const char *path,struct dwa_config *c)
{
    int fd,ok=0;struct stat st,before;ssize_t n;
    memset(c,0,sizeof(*c));
    /* Panther accepts O_NOFOLLOW but can still follow a final symlink.
     * Check lstat explicitly and bind it to the opened inode. The production
     * parent directory is root-owned 0700. */
    if(lstat(path,&before)||!S_ISREG(before.st_mode))return 0;
    fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;
    if(fstat(fd,&st)||st.st_dev!=before.st_dev||st.st_ino!=before.st_ino||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0777)!=0600||st.st_nlink!=1||st.st_size!=sizeof(*c))goto done;
    do{n=read(fd,c,sizeof(*c));}while(n<0&&errno==EINTR);
    ok=n==sizeof(*c)&&dwa_config_valid(c);
done:
    close(fd);if(!ok)dwa_config_clear(c,sizeof(*c));return ok;
}
int dwa_config_write(const char *path,const struct dwa_config *c)
{
    char temp[1024];int fd,ok=0;ssize_t n;
    if(geteuid()!=0||!dwa_config_valid(c)||strlen(path)>sizeof(temp)-12)return 0;
    strcpy(temp,path);strcat(temp,".XXXXXX");fd=mkstemp(temp);if(fd<0)return 0;
    if(fchmod(fd,0600)||fchown(fd,0,0))goto done;
    do{n=write(fd,c,sizeof(*c));}while(n<0&&errno==EINTR);
    if(n!=sizeof(*c)||fsync(fd))goto done;
    if(rename(temp,path))goto done;ok=1;
done:
    close(fd);if(!ok)unlink(temp);return ok;
}
