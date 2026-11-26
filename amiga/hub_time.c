/* hub_time.c - the time for the certificate check.
 *
 * A certificate is valid from / to a date; an Amiga without a battery clock
 * says 1978 and every certificate is "not yet valid". When the clock of the
 * Amiga is older than this program, the time is asked from an SNTP server
 * (UDP 123) and used only for the check - the clock of the Amiga is not
 * changed. */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netdb.h>

#include <string.h>
#include <stdio.h>

#include "hub_time.h"

/* Stop (hub_https.c): Ctrl-C to the process ends the wait at once */
extern volatile int hub_break;

/* waits for an answer at most secs; Ctrl-C sets hub_break */
static long wait_read(long sock, long secs)
{
  fd_set rd;
  struct timeval tv;
  ULONG sig = SIGBREAKF_CTRL_C;
  long n;
  FD_ZERO(&rd);
  FD_SET(sock, &rd);
  tv.tv_secs = secs;
  tv.tv_micro = 0;
  n = WaitSelect(sock + 1, &rd, NULL, NULL, &tv, &sig);
  if (sig & SIGBREAKF_CTRL_C) hub_break = 1;
  return hub_break ? 0 : n;
}

/* 1978-01-01 - 1970-01-01 */
#define AMIGA_EPOCH 252460800UL
#define NTP_TO_UNIX 2208988800UL

ULONG hub_amiga_now(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return AMIGA_EPOCH + (ULONG)ds.ds_Days * 86400UL +
         (ULONG)ds.ds_Minute * 60UL + (ULONG)ds.ds_Tick / TICKS_PER_SECOND;
}

ULONG hub_sntp(const char *server, char *err, int errlen)
{
  struct hostent *he;
  struct sockaddr_in addr;
  UBYTE pkt[48];
  long sock;
  int attempt;
  ULONG result = 0;

  he = gethostbyname((STRPTR)server);
  if (he == NULL) {
    snprintf(err, errlen, "Time server %s not found.", server);
    return 0;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(123);
  addr.sin_len = sizeof(addr);
  memcpy(&addr.sin_addr, he->h_addr, he->h_length);
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    snprintf(err, errlen, "No socket.");
    return 0;
  }
  for (attempt = 0; attempt < 3 && result == 0 && !hub_break; attempt++) {
    long n;
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x1B;                    /* LI 0, version 3, mode 3 (client) */
    if (sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&addr,
               sizeof(addr)) != sizeof(pkt)) {
      continue;
    }
    if (wait_read(sock, 2) <= 0) continue;
    n = recv(sock, pkt, sizeof(pkt), 0);
    if (n >= 48 && (pkt[0] & 7) == 4) {   /* mode 4: server */
      ULONG secs = ((ULONG)pkt[40] << 24) | ((ULONG)pkt[41] << 16) |
                   ((ULONG)pkt[42] << 8) | pkt[43];
      if (secs > NTP_TO_UNIX) result = secs - NTP_TO_UNIX;
    }
  }
  CloseSocket(sock);
  if (result == 0) snprintf(err, errlen, "No answer from time server %s.", server);
  return result;
}

/* --- the time from the Date header of a plain HTTP answer ------------------ *
 * When no time server answers (UDP 123 blocked by a router or a firewall),
 * port 80 is nearly always open: "HEAD / HTTP/1.1" to a big web server and
 * its "Date: Wed, 25 Nov 2026 14:23:11 GMT". Not checked, like NTP; used only
 * for the certificate check. */

static int month_of(const char *m)
{
  static const char *const names = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int i;
  for (i = 0; i < 12; i++) {
    if (strncmp(names + i * 3, m, 3) == 0) return i + 1;
  }
  return 0;
}

/* days since 1970-01-01 of a date (proleptic Gregorian, no 64-bit) */
static long days_of(long y, long m, long d)
{
  long era, yoe, doy, doe;
  y -= m <= 2;
  era = y / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

/* "Wed, 25 Nov 2026 14:23:11 GMT" -> Unix seconds, 0 when not this form */
static ULONG parse_http_date(const char *v)
{
  int d, y, hh, mm, ss;
  char mon[4];
  const char *comma = strchr(v, ',');
  int m;
  if (comma == NULL) return 0;
  if (sscanf(comma + 1, " %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return 0;
  m = month_of(mon);
  if (m == 0 || y < 2024 || y > 2100 || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 60) return 0;
  return (ULONG)days_of(y, m, d) * 86400UL + (ULONG)hh * 3600UL + (ULONG)mm * 60UL + (ULONG)ss;
}

ULONG hub_http_time(const char *host, char *err, int errlen)
{
  struct hostent *he;
  struct sockaddr_in addr;
  char buf[2048], req[200];
  long sock, n;
  int len = 0, reqlen;
  ULONG result = 0;
  struct timeval tv;

  he = gethostbyname((STRPTR)host);
  if (he == NULL) {
    snprintf(err, errlen, "Host %s not found.", host);
    return 0;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(80);
  addr.sin_len = sizeof(addr);
  memcpy(&addr.sin_addr, he->h_addr, he->h_length);
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    snprintf(err, errlen, "No socket.");
    return 0;
  }
  tv.tv_secs = 5;
  tv.tv_micro = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    snprintf(err, errlen, "No connection to %s:80 (errno %ld).", host, (long)Errno());
    CloseSocket(sock);
    return 0;
  }
  reqlen = snprintf(req, sizeof(req), "HEAD / HTTP/1.1\r\nHost: %s\r\n"
                    "User-Agent: WarpHub (Amiga)\r\nConnection: close\r\n\r\n", host);
  if (send(sock, req, reqlen, 0) != reqlen) {
    snprintf(err, errlen, "%s:80 does not take the request.", host);
    CloseSocket(sock);
    return 0;
  }
  /* the headers (up to the empty line), at most 5 s per piece */
  while (len < (int)sizeof(buf) - 1) {
    if (wait_read(sock, 5) <= 0) break;
    n = recv(sock, buf + len, sizeof(buf) - 1 - len, 0);
    if (n <= 0) break;
    len += n;
    buf[len] = 0;
    if (strstr(buf, "\r\n\r\n") != NULL) break;
  }
  CloseSocket(sock);
  buf[len] = 0;
  {
    char *p = buf;
    while (p != NULL && *p) {
      if ((p[0] == 'D' || p[0] == 'd') && strncmp(p + 1, "ate:", 4) == 0) {
        result = parse_http_date(p + 5);
        break;
      }
      p = strchr(p, '\n');
      if (p != NULL) p++;
    }
  }
  if (result == 0) snprintf(err, errlen, "No time in the answer of %s:80.", host);
  return result;
}

/* time servers tried one after another (UDP sometimes gets lost), then the
 * Date of web servers (port 80) */
static const char *const ntp_servers[] = {
  "pool.ntp.org", "time.cloudflare.com", "time.google.com", "europe.pool.ntp.org", NULL
};
static const char *const http_servers[] = {
  "raw.githubusercontent.com", "www.google.com", "www.cloudflare.com", NULL
};

ULONG hub_net_time(const char *ntp, char *how, int howlen, char *err, int errlen)
{
  ULONG t = 0;
  int i;
  if (ntp != NULL) {
    t = hub_sntp(ntp, err, errlen);
    if (t) snprintf(how, howlen, "NTP %s", ntp);
  } else {
    for (i = 0; t == 0 && !hub_break && ntp_servers[i] != NULL; i++) {
      t = hub_sntp(ntp_servers[i], err, errlen);
      if (t) snprintf(how, howlen, "NTP %s", ntp_servers[i]);
    }
  }
  for (i = 0; t == 0 && !hub_break && http_servers[i] != NULL; i++) {
    t = hub_http_time(http_servers[i], err, errlen);
    if (t) snprintf(how, howlen, "HTTP Date of %s", http_servers[i]);
  }
  if (t == 0) {
    snprintf(err, errlen, "No time server (NTP, UDP 123) and no web server (HTTP, port 80) "
             "answers.");
  }
  return t;
}
