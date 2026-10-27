#ifndef DWA121_DATA_PN_H
#define DWA121_DATA_PN_H
/* Internal 48-bit little-endian arithmetic, independent of host word size. */
static int dwa_pn_greater(const unsigned char a[6],const unsigned char b[6])
{
    int i;
    for(i=5;i>=0;--i)if(a[i]!=b[i])return a[i]>b[i];
    return 0;
}
static int dwa_pn_next(unsigned char pn[6])
{
    int i;
    for(i=0;i<6&&pn[i]==255;++i){}
    if(i==6)return -1; /* Never wrap, even on subsequent calls. */
    ++pn[i];while(i)pn[--i]=0;
    return 0;
}
#endif
