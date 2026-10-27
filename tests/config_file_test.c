/* Root-only integration test. All files live in a new private temp directory. */
#include "../src/config/config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void rejected(const char *path)
{
    struct dwa_config out;unsigned int i;memset(&out,0x55,sizeof(out));
    assert(!dwa_config_read(path,&out));
    for(i=0;i<sizeof(out);++i)assert(!((unsigned char *)&out)[i]);
}
int main(void)
{
    char dir[]="/private/tmp/dwa121-config-test.XXXXXX",path[256],other[256];
    struct dwa_config c,out;struct stat st;int fd;
    assert(geteuid()==0&&mkdtemp(dir));
    strcpy(path,dir);strcat(path,"/network.conf");strcpy(other,dir);strcat(other,"/alias");
    memset(&c,0,sizeof(c));memcpy(c.magic,"DWA121\001",8);c.channel=11;c.ssid_length=4;memcpy(c.ssid,"test",4);memset(c.pmk,0x55,32);
    rejected(path);assert(dwa_config_write(path,&c));assert(!stat(path,&st)&&st.st_uid==0&&(st.st_mode&0777)==0600&&st.st_nlink==1);
    assert(dwa_config_read(path,&out)&&!memcmp(&out,&c,sizeof(c)));
    assert(!chmod(path,0644));rejected(path);assert(!chmod(path,0600));
    assert(!symlink(path,other));rejected(other);assert(!unlink(other));
    assert(!link(path,other));rejected(path);assert(!unlink(other));
    assert(!chown(path,501,0));rejected(path);assert(!chown(path,0,0));
    fd=open(path,O_WRONLY|O_TRUNC);assert(fd>=0&&write(fd,"bad",3)==3&&!close(fd));rejected(path);
    assert(dwa_config_write(path,&c));assert(dwa_config_read(path,&out));
    assert(!unlink(path)&&!rmdir(dir));dwa_config_clear(&c,sizeof(c));dwa_config_clear(&out,sizeof(out));
    puts("PASS: root-only config roundtrip, 0600, atomic replacement, symlink/hardlink/owner/mode/truncation rejection and wipe");return 0;
}
