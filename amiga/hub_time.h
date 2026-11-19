#ifndef HUB_TIME_H
#define HUB_TIME_H

#include <exec/types.h>

/* The clock of the Amiga as Unix seconds (local time, no zone). */
ULONG hub_amiga_now(void);

/* Unix seconds from an SNTP server, 0 on a failure (err says why).
 * Needs SocketBase open. */
ULONG hub_sntp(const char *server, char *err, int errlen);

#endif
