/* hub_clock.c - setting the clock of the Amiga, only when the user asks
 * (window "Clock", WarpHubCLI SETCLOCK). Nothing here runs by itself. */

#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <resources/battclock.h>
#include <proto/exec.h>
#include <proto/battclock.h>

#include <stdio.h>
#include <string.h>

#include "hub_clock.h"

/* 1978-01-01 - 1970-01-01: the Amiga counts from 1978 */
#define AMIGA_EPOCH 252460800UL

struct Library *BattClockBase = NULL;

LONG hub_days_from_civil(LONG y, LONG m, LONG d)
{
  LONG era, yoe, doy, doe;
  y -= m <= 2;
  era = y / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void hub_civil_from_days(LONG days, LONG *y, LONG *m, LONG *d)
{
  LONG z = days + 719468, era = z / 146097, doe = z - era * 146097;
  LONG yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  LONG doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yoe + era * 400 + (*m <= 2);
}

/* 01:00 UTC of the last Sunday of a month with 31 days (March, October) */
static ULONG last_sunday(LONG y, LONG m)
{
  LONG days = hub_days_from_civil(y, m, 31);
  days -= (days + 4) % 7;                 /* 1970-01-01 was a Thursday (4) */
  return (ULONG)days * 86400UL + 3600UL;
}

int hub_eu_summer(ULONG utc)
{
  LONG y, m, d;
  hub_civil_from_days((LONG)(utc / 86400UL), &y, &m, &d);
  return utc >= last_sunday(y, 3) && utc < last_sunday(y, 10);
}

ULONG hub_local_of_utc(ULONG utc, LONG zone_min, int dst)
{
  ULONG t = utc + (ULONG)(zone_min * 60L);
  if (dst && hub_eu_summer(utc)) t += 3600UL;
  return t;
}

void hub_clock_text(ULONG local, char *out, int outlen)
{
  LONG y, m, d;
  ULONG s = local % 86400UL;
  hub_civil_from_days((LONG)(local / 86400UL), &y, &m, &d);
  snprintf(out, outlen, "%02ld.%02ld.%04ld %02lu:%02lu:%02lu", (long)d, (long)m, (long)y,
           s / 3600UL, (s / 60UL) % 60UL, s % 60UL);
}

static int number(const char **p, int digits_max, LONG *v)
{
  int n = 0;
  *v = 0;
  while (**p == ' ') (*p)++;
  while (**p >= '0' && **p <= '9' && n < digits_max) {
    *v = *v * 10 + (**p - '0');
    (*p)++;
    n++;
  }
  return n > 0;
}

ULONG hub_clock_parse(const char *date, const char *time)
{
  static const char mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  LONG d, m, y, h = 0, mi = 0, s = 0;
  const char *p = date;
  if (!number(&p, 2, &d) || (*p != '.' && *p != '-' && *p != '/')) return 0;
  p++;
  if (!number(&p, 2, &m) || (*p != '.' && *p != '-' && *p != '/')) return 0;
  p++;
  if (!number(&p, 4, &y)) return 0;
  while (*p == ' ') p++;
  if (*p) return 0;
  p = time;
  if (!number(&p, 2, &h)) return 0;
  if (*p == ':') {
    p++;
    if (!number(&p, 2, &mi)) return 0;
    if (*p == ':') {
      p++;
      if (!number(&p, 2, &s)) return 0;
    }
  }
  while (*p == ' ') p++;
  if (*p) return 0;
  if (y < 1978 || y > 2105 || m < 1 || m > 12 || d < 1 || d > mdays[m - 1] ||
      h > 23 || mi > 59 || s > 59) {
    return 0;
  }
  if (m == 2 && d == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 0;
  return (ULONG)hub_days_from_civil(y, m, d) * 86400UL + (ULONG)(h * 3600L + mi * 60L + s);
}

int hub_clock_has_batt(void)
{
  return OpenResource((CONST_STRPTR)BATTCLOCKNAME) != NULL;
}

int hub_clock_set(ULONG local, ULONG micro, int write_batt, int *batt, char *err, int errlen)
{
  struct MsgPort *port;
  struct timerequest *tr;
  ULONG secs;
  int ok = 0;
  *batt = 0;
  if (local < AMIGA_EPOCH) {
    snprintf(err, errlen, "date before 1978");
    return 0;
  }
  secs = local - AMIGA_EPOCH;
  port = CreateMsgPort();
  tr = port != NULL ? (struct timerequest *)CreateIORequest(port, sizeof(*tr)) : NULL;
  if (tr == NULL) {
    snprintf(err, errlen, "no memory");
  } else if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0) != 0) {
    snprintf(err, errlen, "timer.device does not open");
  } else {
    tr->tr_node.io_Command = TR_SETSYSTIME;
    tr->tr_time.tv_secs = secs;
    tr->tr_time.tv_micro = micro;
    if (DoIO((struct IORequest *)tr) == 0) ok = 1;
    else snprintf(err, errlen, "timer.device: error %ld", (long)tr->tr_node.io_Error);
    CloseDevice((struct IORequest *)tr);
  }
  if (tr != NULL) DeleteIORequest((struct IORequest *)tr);
  if (port != NULL) DeleteMsgPort(port);
  if (ok && write_batt) {
    BattClockBase = (struct Library *)OpenResource((CONST_STRPTR)BATTCLOCKNAME);
    if (BattClockBase != NULL) {
      WriteBattClock(secs);
      *batt = 1;
    }
  }
  return ok;
}
