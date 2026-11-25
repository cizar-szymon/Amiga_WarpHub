#ifndef HUB_TIME_H
#define HUB_TIME_H

#include <exec/types.h>

/* The clock of the Amiga as Unix seconds (local time, no zone). */
ULONG hub_amiga_now(void);

/* Unix seconds from an SNTP server, 0 on a failure (err says why).
 * Needs SocketBase open. */
ULONG hub_sntp(const char *server, char *err, int errlen);

/* Unix seconds from the Date header of a plain HTTP answer (port 80), 0 on
 * a failure. Needs SocketBase open. */
ULONG hub_http_time(const char *host, char *err, int errlen);

/* The time for the certificate check: the SNTP server ntp (NULL: a list of
 * them), then the Date of web servers. how: where it came from. 0 when none
 * answers (err says so). */
ULONG hub_net_time(const char *ntp, char *how, int howlen, char *err, int errlen);

/* 1 when the clock of the Amiga can be believed for the certificate check:
 * not older than this program and not more than 10 years after it (a clock
 * without a battery shows 1978; a wrong one was seen at 2097 on Amiga 2) */
#define HUB_CLOCK_OK(now) ((now) >= BUILD_UNIX && (now) - BUILD_UNIX < 10UL * 366UL * 86400UL)

#endif
