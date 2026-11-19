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
  for (attempt = 0; attempt < 3 && result == 0; attempt++) {
    fd_set rd;
    struct timeval tv;
    long n;
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x1B;                    /* LI 0, version 3, mode 3 (client) */
    if (sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&addr,
               sizeof(addr)) != sizeof(pkt)) {
      continue;
    }
    FD_ZERO(&rd);
    FD_SET(sock, &rd);
    tv.tv_secs = 2;
    tv.tv_micro = 0;
    if (WaitSelect(sock + 1, &rd, NULL, NULL, &tv, NULL) <= 0) continue;
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
