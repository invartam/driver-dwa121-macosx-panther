/* Semantic rollback verification; never prints preference values/secrets. */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
static CFPropertyListRef readPlist(const char *path)
{
    FILE *f=fopen(path,"rb");long n;unsigned char *bytes;CFDataRef data;CFPropertyListRef p;
    if(!f)return 0;
    if(fseek(f,0,SEEK_END)||(n=ftell(f))<1||n>4194304||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
    bytes=malloc(n);if(!bytes){fclose(f);return 0;}
    if(fread(bytes,1,n,f)!=(size_t)n){free(bytes);fclose(f);return 0;}
    fclose(f);data=CFDataCreate(0,bytes,n);free(bytes);if(!data)return 0;
    p=CFPropertyListCreateFromXMLData(0,data,kCFPropertyListImmutable,0);CFRelease(data);return p;
}
int main(int argc,char **argv)
{
    CFPropertyListRef a,b;int ok;if(argc!=3)return 1;
    a=readPlist(argv[1]);b=readPlist(argv[2]);ok=a&&b&&CFEqual(a,b);
    if(a)CFRelease(a);if(b)CFRelease(b);
    puts(ok?"PASS: all preference values restored":"FAIL: preferences differ or cannot be read");return ok?0:1;
}
