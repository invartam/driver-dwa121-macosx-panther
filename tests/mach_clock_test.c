/* Links the actual hostap os_unix.c with only Mach's clock calls replaced.
 * Synthetic PPC timebase vectors are independent of wall clock and Wi-Fi. */
#include <stdio.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include "utils/includes.h"
#include "utils/os.h"
static uint64_t ticks;
uint64_t dwa_test_ticks(void){return ticks;}
kern_return_t dwa_test_timebase(mach_timebase_info_t info)
{info->numer=1000000000U;info->denom=25000000U;return KERN_SUCCESS;}
int main(void)
{
    const unsigned int seconds[]={0,737,738,1475,1476,86400,2592000};
    unsigned int i;struct os_reltime t,previous={0,0};
    for(i=0;i<sizeof(seconds)/sizeof(seconds[0]);++i){
        ticks=(uint64_t)seconds[i]*25000000U+123456U;
        if(os_get_reltime(&t)||t.sec!=seconds[i]||t.usec!=4938||t.sec<previous.sec){
            fprintf(stderr,"FAIL: Mach relative clock at %u seconds\n",seconds[i]);return 1;
        }
        previous=t;
    }
    puts("PASS: actual hostap Mach clock across 64-bit multiplication overflow, 1 day and 30 days");
    return 0;
}
