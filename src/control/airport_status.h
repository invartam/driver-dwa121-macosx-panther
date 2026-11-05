#ifndef DWA_AIRPORT_STATUS_H
#define DWA_AIRPORT_STATUS_H
#include "airport_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
/* No IP state or secret is part of this conversion. */
int dwa_airport_status(const struct dwa_airport_target *,const rtl_u8 *,rtl_u8 *,unsigned int);
#ifdef __cplusplus
}
#endif
#endif
