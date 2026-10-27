#ifndef DWA121_WPA_PLATFORM_H
#define DWA121_WPA_PLATFORM_H
/* Panther's endian.h predates Apple's underscored names used by hostap.
 * Without the common.h compatibility patch undefined endian macros evaluate
 * as 0 == 0 and silently selects little endian on a PowerPC. Do not guess
 * from compiler architecture; use the installed system's declarations. */
#ifdef __APPLE__
#include <machine/endian.h>
#if defined(_BYTE_ORDER) && defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN)
#if _BYTE_ORDER != _BIG_ENDIAN && _BYTE_ORDER != _LITTLE_ENDIAN
#error Unsupported Darwin byte order
#endif
#elif defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && defined(BIG_ENDIAN)
#if BYTE_ORDER != BIG_ENDIAN && BYTE_ORDER != LITTLE_ENDIAN
#error Unsupported old Darwin byte order
#endif
#else
#error Missing Darwin endian declarations
#endif
#endif
#endif
