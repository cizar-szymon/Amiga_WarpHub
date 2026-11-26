/* hub_clock - setting the clock of the Amiga (only when the user asks: the
 * window "Clock" of WARP HUB, WarpHubCLI SETCLOCK).
 *
 * Times are Unix seconds. The Amiga keeps local time without a zone, so a
 * time from the internet (UTC) becomes local with the zone of the window and
 * the EU summer time rule. */
#ifndef HUB_CLOCK_H
#define HUB_CLOCK_H

#include <exec/types.h>

/* days since 1970-01-01 of a date (y >= 1970), and back */
LONG hub_days_from_civil(LONG y, LONG m, LONG d);
void hub_civil_from_days(LONG days, LONG *y, LONG *m, LONG *d);

/* 1 when the UTC time is in the EU summer time: from the last Sunday of
 * March 01:00 UTC to the last Sunday of October 01:00 UTC */
int hub_eu_summer(ULONG utc);

/* local Unix seconds of a UTC time: zone in minutes east of UTC (+60 =
 * UTC+1), dst: add an hour in the EU summer time */
ULONG hub_local_of_utc(ULONG utc, LONG zone_min, int dst);

/* "26.11.2026 14:05:09" of local Unix seconds */
void hub_clock_text(ULONG local, char *out, int outlen);

/* "DD.MM.YYYY" and "HH:MM:SS" -> local Unix seconds, 0 on a bad one
 * (1978..2105, as the clock of the Amiga) */
ULONG hub_clock_parse(const char *date, const char *time);

/* Sets the system clock (timer.device TR_SETSYSTIME) to local Unix seconds
 * and micro; write_batt: also the battery clock (battclock.resource) when
 * there is one. 1 on success; *batt: 1 when the battery clock was written
 * too. err says why it failed. */
int hub_clock_set(ULONG local, ULONG micro, int write_batt, int *batt, char *err, int errlen);

/* 1 when battclock.resource is there */
int hub_clock_has_batt(void);

#endif
