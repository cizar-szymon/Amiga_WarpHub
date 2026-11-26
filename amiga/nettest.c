/* WarpNetTest - how fast and how well this Amiga reaches GitHub and the
 * private source of WARP HUB. From the icon (a window) or the Shell; every
 * line also goes to RAM:WarpNetTest.txt (to send with a bug report).
 *
 * Each measurement is one row with a mark ([ OK ] / [SLOW] / [FAIL]); at the
 * end a summary in plain words: what works, what does not, what it means for
 * WARP HUB (e.g. how long its first start takes) and what to do.
 * Polish or English after LANGUAGE in WarpHub.prefs (PROGDIR: or
 * SYS:WarpApps), which also gives the private source (PRIVATE, PRIVATE-CERT,
 * PRIVATE-PASS). No address in the code. Every wait is limited (hub_https:
 * 20 s), Ctrl-C stops. */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <workbench/startup.h>
#include <workbench/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>
#include <libraries/bsdsocket.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/filio.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hub_mem.h"
#include "hub_https.h"
#include "hub_time.h"
#include "hub_util.h"

#ifndef BUILD_UNIX
#define BUILD_UNIX 1795046400UL
#endif

#define NETTEST_VERSION "0.4"
static const char vers[] = "$VER: WarpNetTest " NETTEST_VERSION " (25.11.2026)";

unsigned long __stack = 65536;
extern void __stkinit(void);
void (*const nettest_stackSwap)(void) = __stkinit;

extern struct WBStartup *_WBenchMsg;
extern struct Library *SocketBase;
extern struct ExecBase *SysBase;

#define RAW "raw.githubusercontent.com"
#define LOG_FILE "RAM:WarpNetTest.txt"
#define SMALL "/cizar-szymon/Amiga_WarpHub/main/releases/amiga/WarpHub.info"
#define CATALOG "/cizar-szymon/Amiga_WarpHub/main/catalog/catalog.txt"
#define BIGZIP "/cizar-szymon/Amiga_game_Settlers/amiga-port/releases/amiga/Settlers-0.140.zip"

/* --- texts ------------------------------------------------------------------ */

static int pl;                        /* Polish (LANGUAGE pl) */
#define T(p, e) (pl ? (p) : (e))

/* --- output: the window (with bold), the log (plain) ------------------------ */

static BPTR out_fh;
static int own_window;
static char *logbuf;
static ULONG loglen, logcap;
static char err[300];

static void put(const char *s, int bold)
{
  int n = (int)strlen(s);
  if (out_fh != 0) {
    if (bold && own_window) Write(out_fh, "\x9b" "1m", 3);
    Write(out_fh, (APTR)s, n);
    if (bold && own_window) Write(out_fh, "\x9b" "0m", 3);
    Write(out_fh, "\n", 1);
  }
  if (logbuf != NULL && loglen + n + 1 < logcap) {
    memcpy(logbuf + loglen, s, n);
    loglen += n;
    logbuf[loglen++] = '\n';
  }
}

static void say(const char *fmt, ...)
{
  char line[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  put(line, 0);
}

static void head(const char *title)
{
  char line[120];
  put("", 0);
  snprintf(line, sizeof(line), "%s", title);
  put(line, 1);
}

enum { OK, SLOW, FAIL, INFO };

static const char *mark(int m)
{
  switch (m) {
    case OK: return "[ OK ]";
    case SLOW: return pl ? "[WOLNO]" : "[SLOW]";
    case FAIL: return pl ? "[BLAD]" : "[FAIL]";
    default: return "      ";
  }
}

/* one row: mark, what, value */
static void row(int m, const char *what, const char *fmt, ...)
{
  char value[200], line[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(value, sizeof(value), fmt, ap);
  va_end(ap);
  snprintf(line, sizeof(line), " %-7s %-30s %s", mark(m), what, value);
  put(line, 0);
}

static void save_log(void)
{
  if (logbuf != NULL) hub_write_file(LOG_FILE, logbuf, loglen);
}

static int stopped(void)
{
  if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) hub_break = 1;
  return hub_break;
}

/* --- the results ------------------------------------------------------------- */

static struct {
  int dns_ok;            /* GitHub found */
  int tcp443, tcp80;     /* 1 = connected */
  ULONG tcp443_ms;
  int ntp_ok, http_time_ok;
  long clock_off;        /* clock of the Amiga - network time, s (local zone included) */
  int have_time;
  int gh_ok;             /* the first HTTPS request went */
  ULONG gh_first_ms, gh_small_ms;
  ULONG gh_cat_kbs, gh_zip_kbs;
  int gh_games;          /* games of the catalog */
  int priv_set, priv_tcp, priv_ok, priv_pass, priv_status;
  long priv_tcp_ms;
  int priv_timeout, gh_timeout;    /* the TLS / data wait ended by the time limit */
  ULONG priv_first_ms, priv_kbs;
  char gh_err[200], priv_err[200];
} r;

/* --- names and TCP --------------------------------------------------------- */

static int test_dns(const char *host)
{
  ULONG t0 = hub_ms(), dt;
  struct hostent *he = gethostbyname((STRPTR)host);
  dt = hub_ms() - t0;
  if (he == NULL) {
    row(FAIL, host, T("nie znaleziono (%lu ms)", "not found (%lu ms)"), (unsigned long)dt);
    return 0;
  }
  {
    UBYTE *a = (UBYTE *)he->h_addr;
    row(dt > 2000 ? SLOW : OK, host, "%u.%u.%u.%u  %lu ms", a[0], a[1], a[2], a[3],
        (unsigned long)dt);
  }
  return 1;
}

/* TCP connect only (no TLS), at most secs; ms or -1 */
static long test_tcp(const char *host, int port, int secs)
{
  struct hostent *he = gethostbyname((STRPTR)host);
  struct sockaddr_in addr;
  long sock, one = 1, so = 0, result = -1;
  socklen_t solen = sizeof(so);
  char what[80];
  ULONG t0;
  snprintf(what, sizeof(what), "%s:%d", host, port);
  if (he == NULL) {
    row(FAIL, what, T("nie znaleziono nazwy", "name not found"));
    return -1;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_len = sizeof(addr);
  memcpy(&addr.sin_addr, he->h_addr, he->h_length);
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return -1;
  IoctlSocket(sock, FIONBIO, (APTR)&one);
  t0 = hub_ms();
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
    result = 0;
  } else if (Errno() == EINPROGRESS || Errno() == EALREADY) {
    fd_set wr;
    struct timeval tv;
    FD_ZERO(&wr);
    FD_SET(sock, &wr);
    tv.tv_secs = secs;
    tv.tv_micro = 0;
    if (WaitSelect(sock + 1, NULL, &wr, NULL, &tv, NULL) > 0 &&
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so, &solen) == 0 && so == 0) {
      result = 0;
    }
  }
  if (result == 0) {
    result = (long)(hub_ms() - t0);
    row(result > 1000 ? SLOW : OK, what, T("polaczenie w %ld ms", "connected in %ld ms"), result);
  } else {
    row(FAIL, what, T("brak polaczenia po %lu ms (errno %ld)", "no connection after %lu ms (errno %ld)"),
        (unsigned long)(hub_ms() - t0), so ? so : (long)Errno());
  }
  CloseSocket(sock);
  return result;
}

/* --- HTTPS ----------------------------------------------------------------- */

typedef struct Sink {
  ULONG got;
  char *keep;            /* the first bytes (the catalog), or NULL */
  ULONG keep_cap;
} Sink;

static int sink_fn(void *user, const UBYTE *data, LONG len)
{
  Sink *s = (Sink *)user;
  if (s->keep != NULL && s->got < s->keep_cap) {
    ULONG n = s->keep_cap - s->got < (ULONG)len ? s->keep_cap - s->got : (ULONG)len;
    memcpy(s->keep + s->got, data, n);
  }
  s->got += len;
  return !stopped();
}

/* KB/s of n bytes in ms (no 64-bit) */
static ULONG kbs(ULONG bytes, ULONG ms)
{
  if (ms == 0) ms = 20;
  return (bytes / 1024UL) * 1000UL / ms;
}

static int speed_mark(ULONG k)
{
  return k < 30 ? FAIL : k < 100 ? SLOW : OK;
}

/* one GET on c: -1 failed (err), else the status; *ms, *bytes */
static int get(HubConn *c, const char *path, Sink *s, ULONG *ms)
{
  char p[600];
  ULONG t0 = hub_ms();
  int status;
  hub_url_path(p, sizeof(p), path);
  status = hub_get(c, p, sink_fn, s, NULL, 0, err, sizeof(err));
  *ms = hub_ms() - t0;
  return status;
}

static void test_github(void)
{
  HubConn *c = hub_conn_new(RAW, 443);
  Sink s;
  ULONG ms, sum = 0;
  int i, status;
  if (c == NULL) return;
  memset(&s, 0, sizeof(s));
  status = get(c, SMALL, &s, &ms);
  if (status != 200) {
    row(FAIL, T("polaczenie TLS", "TLS connection"), "%s", status < 0 ? err : "HTTP error");
    snprintf(r.gh_err, sizeof(r.gh_err), "%s", status < 0 ? err : "HTTP error");
    r.gh_timeout = status < 0 && strstr(err, "within") != NULL;
    hub_conn_free(c);
    return;
  }
  r.gh_ok = 1;
  r.gh_first_ms = ms;
  row(ms > 8000 ? FAIL : ms > 3000 ? SLOW : OK, T("polaczenie TLS + maly plik", "TLS connection + small file"),
      "%lu ms", (unsigned long)ms);
  row(INFO, "", "%s", hub_conn_cipher(c));
  for (i = 0; i < 5 && !stopped(); i++) {
    memset(&s, 0, sizeof(s));
    if (get(c, SMALL, &s, &ms) != 200) break;
    sum += ms;
  }
  if (i == 5) {
    r.gh_small_ms = sum / 5;
    row(r.gh_small_ms > 1000 ? FAIL : r.gh_small_ms > 300 ? SLOW : OK,
        T("male zapytanie (sr. z 5)", "small request (avg of 5)"), "%lu ms", (unsigned long)r.gh_small_ms);
  } else {
    row(FAIL, T("male zapytanie (sr. z 5)", "small request (avg of 5)"), "%s", err);
  }
  if (!stopped()) {
    memset(&s, 0, sizeof(s));
    s.keep_cap = 512UL * 1024UL;
    s.keep = (char *)hub_malloc(s.keep_cap + 1);
    status = get(c, CATALOG, &s, &ms);
    if (status == 200) {
      r.gh_cat_kbs = kbs(s.got, ms);
      row(speed_mark(r.gh_cat_kbs), T("katalog gier", "catalog of the games"), "%lu KB, %lu ms, %lu KB/s",
          (unsigned long)(s.got / 1024), (unsigned long)ms, (unsigned long)r.gh_cat_kbs);
      if (s.keep != NULL) {
        ULONG n = s.got < s.keep_cap ? s.got : s.keep_cap, k;
        s.keep[n] = 0;
        for (k = 0; k + 5 < n; k++) {
          if ((k == 0 || s.keep[k - 1] == '\n') && strncmp(s.keep + k, "GAME ", 5) == 0) r.gh_games++;
        }
      }
    } else {
      row(FAIL, T("katalog gier", "catalog of the games"), "%s", status < 0 ? err : "HTTP error");
    }
    hub_free(s.keep);
  }
  if (!stopped()) {
    memset(&s, 0, sizeof(s));
    status = get(c, BIGZIP, &s, &ms);
    if (status == 200) {
      r.gh_zip_kbs = kbs(s.got, ms);
      row(speed_mark(r.gh_zip_kbs), T("duzy plik (ZIP)", "big file (ZIP)"), "%lu KB, %lu ms, %lu KB/s",
          (unsigned long)(s.got / 1024), (unsigned long)ms, (unsigned long)r.gh_zip_kbs);
    } else {
      row(FAIL, T("duzy plik (ZIP)", "big file (ZIP)"), "%s", status < 0 ? err : "HTTP error");
    }
  }
  hub_conn_free(c);
}

static void test_private(HubPrefs *p)
{
  char host[128];
  const char *colon;
  int port = 6831, n, status;
  HubConn *c;
  Sink s;
  ULONG ms;
  colon = strrchr(p->priv, ':');
  n = colon != NULL ? (int)(colon - p->priv) : (int)strlen(p->priv);
  if (n <= 0 || n >= (int)sizeof(host)) return;
  memcpy(host, p->priv, n);
  host[n] = 0;
  if (colon != NULL) port = atoi(colon + 1);
  r.priv_tcp_ms = test_tcp(host, port, 10);
  r.priv_tcp = r.priv_tcp_ms >= 0;
  if (!r.priv_tcp || stopped()) return;
  c = hub_conn_new(host, port);
  if (c == NULL) return;
  hub_conn_pin(c, strlen(p->priv_cert) == 64 ? p->priv_cert : "");
  r.priv_pass = p->priv_pass[0] && strlen(p->priv_cert) == 64;
  if (r.priv_pass) hub_conn_auth(c, p->priv_pass);
  memset(&s, 0, sizeof(s));
  status = get(c, "/catalog.txt", &s, &ms);
  r.priv_status = status;
  if (status < 0) {
    row(FAIL, T("polaczenie TLS", "TLS connection"), "%s", err);
    snprintf(r.priv_err, sizeof(r.priv_err), "%s", err);
    r.priv_timeout = strstr(err, "within") != NULL;
  } else if (status == 200) {
    r.priv_ok = 1;
    r.priv_first_ms = ms;
    r.priv_kbs = kbs(s.got, ms);
    row(speed_mark(r.priv_kbs), T("TLS + katalog pelnych paczek", "TLS + catalog of full packages"),
        "%lu KB, %lu ms, %lu KB/s", (unsigned long)(s.got / 1024), (unsigned long)ms,
        (unsigned long)r.priv_kbs);
  } else {
    r.priv_ok = 1;                  /* the server answers; no (right) password */
    r.priv_first_ms = ms;
    row(OK, T("polaczenie TLS", "TLS connection"), T("%lu ms (HTTP %d: %s)", "%lu ms (HTTP %d: %s)"),
        (unsigned long)ms, status,
        status == 401 ? T("haslo nie zapamietane - bez pomiaru pobierania",
                          "no remembered password - no download test")
                      : T("serwer odpowiada", "the server answers"));
  }
  hub_conn_free(c);
}

/* --- the TCP/IP stack and the router -------------------------------------- */

struct Device *TimerBase;
static struct timerequest *treq;
static struct MsgPort *tport;
static ULONG eclock_hz;               /* E-Clock ticks a second (0 = no timer) */

static void timer_open(void)
{
  tport = CreateMsgPort();
  if (tport == NULL) return;
  treq = (struct timerequest *)CreateIORequest(tport, sizeof(struct timerequest));
  if (treq == NULL) return;
  if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)treq, 0) != 0) {
    DeleteIORequest((struct IORequest *)treq);
    treq = NULL;
    return;
  }
  TimerBase = treq->tr_node.io_Device;
  {
    struct EClockVal ev;
    eclock_hz = ReadEClock(&ev);
  }
}

static void timer_close(void)
{
  if (treq != NULL) {
    CloseDevice((struct IORequest *)treq);
    DeleteIORequest((struct IORequest *)treq);
    treq = NULL;
  }
  if (tport != NULL) DeleteMsgPort(tport);
  tport = NULL;
  TimerBase = NULL;
}

/* the low 32 bits of the E-Clock (differences only; ~6000 s before a wrap) */
static ULONG ticks(void)
{
  struct EClockVal ev;
  if (TimerBase == NULL) return hub_ms();
  ReadEClock(&ev);
  return ev.ev_lo;
}

/* ticks -> tenths of a millisecond (no 64-bit; d up to ~60 s) */
static ULONG ticks_01ms(ULONG d)
{
  ULONG hz100 = eclock_hz / 100UL;
  if (TimerBase == NULL || hz100 == 0) return d * 10UL;      /* hub_ms */
  if (d > 40000000UL) return d / hz100 * 100UL;
  return d * 100UL / hz100;
}

static ULONG ms_ticks(ULONG ms)
{
  if (TimerBase == NULL || eclock_hz == 0) return ms;
  return ms * (eclock_hz / 1000UL);
}

static void ip_text(ULONG ip, char *out, int outlen)  /* ip in network order */
{
  snprintf(out, outlen, "%lu.%lu.%lu.%lu", (unsigned long)(ip >> 24),
           (unsigned long)((ip >> 16) & 255), (unsigned long)((ip >> 8) & 255),
           (unsigned long)(ip & 255));
}

static struct {
  char release[80];      /* SBTC_RELEASESTRPTR */
  int warpsocket;        /* warpSOCKET: TCP/IP on the WiFi module of the Warp */
  int warp_bridge;       /* Roadshow on warpNET (the WiFi of the Warp, bridge) */
  int warp_info;         /* cswarp.library answered */
  int warp_mode, warp_link, warp_ipvalid;
  ULONG warp_ip, warp_gw;
  char iface[160];       /* Roadshow: interfaces "name device addr mtu" */
  ULONG router;          /* network order, 0 = not known */
  int router_from;       /* 0 none, 1 stack, 2 WiFi module, 3 option */
} st;

/* cswarp.library netCtl(info, op, arg) at -618: the network state of the
 * WiFi module (mode, link, address, gateway). Called only for 30.65+, as
 * its documentation says. The info block (version 1) is laid out here:
 * version, 6 bytes of state, mac[6], ip, netmask, gateway, dns[2], ... */
static LONG warp_netctl(struct Library *base, UBYTE *info, ULONG op, ULONG arg)
{
  register LONG d0 __asm("d0") = (LONG)op;
  register ULONG d1 __asm("d1") = arg;
  register UBYTE *a0 __asm("a0") = info;
  register ULONG a1 __asm("a1");
  register struct Library *a6 __asm("a6") = base;
  __asm volatile ("jsr a6@(-618:W)"
                  : "+r"(d0), "+r"(d1), "+r"(a0), "=r"(a1)
                  : "r"(a6)
                  : "cc", "memory");
  (void)a1;
  return d0;
}

static ULONG get32(const UBYTE *p)
{
  return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static void warp_info(void)
{
  struct Library *cs = OpenLibrary((CONST_STRPTR)"cswarp.library", 30);
  UBYTE info[160];
  if (cs == NULL) return;
  if ((((ULONG)cs->lib_Version << 16) | cs->lib_Revision) >= ((30UL << 16) | 65UL)) {
    memset(info, 0, sizeof(info));
    info[3] = 1;                                   /* version 1 */
    if (warp_netctl(cs, info, 1 /* INFO */, 0) == 0) {
      st.warp_info = 1;
      st.warp_mode = info[4];                      /* 1 = stack */
      st.warp_link = info[7];
      st.warp_ipvalid = info[8];
      st.warp_ip = get32(info + 16);
      st.warp_gw = get32(info + 24);
    }
  }
  CloseLibrary(cs);
}

/* Roadshow: the default route (a gateway to 0.0.0.0) of GetRouteInfo */
static ULONG roadshow_router(void)
{
  struct rt_msghdr *tab, *m;
  ULONG gw = 0;
  tab = GetRouteInfo(AF_INET, RTF_GATEWAY);
  if (tab == NULL) return 0;
  for (m = tab; m->rtm_msglen != 0 && gw == 0;
       m = (struct rt_msghdr *)((UBYTE *)m + m->rtm_msglen)) {
    UBYTE *p = (UBYTE *)(m + 1), *end = (UBYTE *)m + m->rtm_msglen;
    ULONG dst = 0, g = 0;
    int i, have_dst = 0;
    if (m->rtm_version != RTM_VERSION || !(m->rtm_flags & RTF_GATEWAY)) continue;
    for (i = 0; i < RTAX_MAX && p < end; i++) {
      struct sockaddr *sa = (struct sockaddr *)p;
      int len;
      if (!(m->rtm_addrs & (1 << i))) continue;
      len = sa->sa_len ? sa->sa_len : 4;
      if (sa->sa_family == AF_INET && len >= 8) {
        ULONG a = get32(p + 4);
        if (i == RTAX_DST) {
          dst = a;
          have_dst = 1;
        } else if (i == RTAX_GATEWAY) {
          g = a;
        }
      }
      p += (len + 3) & ~3;
    }
    if ((!have_dst || dst == 0) && g != 0) gw = g;
  }
  FreeRouteInfo(tab);
  return gw;
}

/* Roadshow: the interfaces (not lo0) with their device, address and MTU */
static void roadshow_ifaces(void)
{
  struct List *list = ObtainInterfaceList();
  struct Node *n;
  int len = 0;
  if (list == NULL) return;
  for (n = list->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
    STRPTR dev = NULL;
    LONG mtu = 0;
    struct sockaddr_in a;
    char ip[20], low[64];
    int k;
    if (n->ln_Name == NULL || strncmp(n->ln_Name, "lo", 2) == 0) continue;
    memset(&a, 0, sizeof(a));
    if (QueryInterfaceTags((STRPTR)n->ln_Name, IFQ_DeviceName, (ULONG)&dev, IFQ_Address, (ULONG)&a,
                           IFQ_MTU, (ULONG)&mtu, TAG_END) != 0) {
      continue;
    }
    ip_text(a.sin_addr.s_addr, ip, sizeof(ip));
    snprintf(low, sizeof(low), "%s", dev != NULL ? (char *)dev : "?");
    for (k = 0; low[k]; k++) {
      if (low[k] >= 'A' && low[k] <= 'Z') low[k] += 32;
    }
    if (strstr(low, "warpnet") != NULL) st.warp_bridge = 1;
    len += snprintf(st.iface + len, sizeof(st.iface) - len, "%s%s %s %s MTU %ld",
                    len ? ", " : "", n->ln_Name, dev != NULL ? (char *)dev : "?", ip, (long)mtu);
    if (len >= (int)sizeof(st.iface)) break;
  }
  ReleaseInterfaceList(list);
}

/* which stack runs, through what, and the router; option: ROUTER=<address> */
static void test_stack(const char *router_opt)
{
  STRPTR rel = NULL;
  LONG routing = 0, ifapi = 0;
  char a[20], b[20];
  int k;
  if (SocketBaseTags(SBTM_GETREF(SBTC_RELEASESTRPTR), (ULONG)&rel, TAG_END) == 0 && rel != NULL) {
    snprintf(st.release, sizeof(st.release), "%s", (char *)rel);
  }
  for (k = 0; st.release[k]; k++) {
    if (st.release[k] == '\r' || st.release[k] == '\n') st.release[k] = 0;
  }
  st.warpsocket = strstr(st.release, "warpSOCKET") != NULL;
  if (st.warpsocket && strstr(st.release, " (") != NULL) *strstr(st.release, " (") = 0;
  warp_info();
  if (!st.warpsocket) {
    SocketBaseTags(SBTM_GETREF(SBTC_HAVE_ROUTING_API), (ULONG)&routing,
                   SBTM_GETREF(SBTC_HAVE_INTERFACE_API), (ULONG)&ifapi, TAG_END);
    if (ifapi) roadshow_ifaces();
    if (routing) {
      st.router = roadshow_router();
      if (st.router) st.router_from = 1;
    }
  }
  if (st.warp_info && st.warp_mode == 1 && st.warp_ipvalid && st.warp_gw != 0 && st.router == 0) {
    st.router = st.warp_gw;
    st.router_from = 2;
  }
  if (router_opt != NULL && router_opt[0]) {
    ULONG ip = inet_addr((STRPTR)router_opt);
    if (ip == 0xffffffffUL) {
      struct hostent *he = gethostbyname((STRPTR)router_opt);
      ip = he != NULL ? get32((UBYTE *)he->h_addr) : 0;
    }
    if (ip != 0 && ip != 0xffffffffUL) {
      st.router = ip;
      st.router_from = 3;
    }
  }

  if (st.warpsocket) {
    row(INFO, T("stos TCP/IP", "TCP/IP stack"), T("%s - WiFi karty Warp (tryb stack)",
        "%s - WiFi of the Warp card (stack mode)"), st.release);
  } else if (st.warp_bridge) {
    row(INFO, T("stos TCP/IP", "TCP/IP stack"), T("%s - WiFi karty Warp (warpNET, bridge)",
        "%s - WiFi of the Warp card (warpNET, bridge)"), st.release[0] ? st.release : "?");
  } else {
    row(INFO, T("stos TCP/IP", "TCP/IP stack"), "%s", st.release[0] ? st.release : "?");
  }
  if (st.iface[0]) row(INFO, T("interfejsy", "interfaces"), "%s", st.iface);
  if (st.warp_info && st.warp_mode == 1) {
    ip_text(st.warp_ip, a, sizeof(a));
    ip_text(st.warp_gw, b, sizeof(b));
    row(st.warp_link ? INFO : FAIL, T("WiFi karty Warp", "WiFi of the Warp card"),
        st.warp_link ? (st.warp_ipvalid ? T("polaczone, adres %s, brama %s", "connected, address %s, gateway %s")
                                        : T("polaczone, czeka na adres (DHCP)", "connected, waiting for DHCP"))
                     : T("brak polaczenia z siecia WiFi", "not connected to a WiFi network"), a, b);
  }
  if (st.router) {
    ip_text(st.router, a, sizeof(a));
    row(INFO, T("router (brama)", "router (gateway)"), "%s%s", a,
        st.router_from == 3 ? T("  (z opcji ROUTER)", "  (of the option ROUTER)") : "");
  } else {
    row(INFO, T("router (brama)", "router (gateway)"),
        T("nieznany - mozna podac ROUTER=<adres> (Tool Types / Shell)",
          "not known - give ROUTER=<address> (Tool Types / Shell)"));
  }
}

/* --- ping (ICMP echo through a raw socket) -------------------------------- */

#define PING_COUNT 20
#define PING_EVERY_MS 100
#define PING_TAIL_MS 1000
#define PING_SMALL 56
#define PING_BIG 1400           /* warpSOCKET: at most 1468 data bytes */

typedef struct PingStat {
  int sent, got, errors;      /* errors: ICMP unreachable / time exceeded */
  ULONG min, max, sum, jit;   /* tenths of a ms */
  int ran;                    /* 0 = not run */
} PingStat;

static int raw_failed;         /* the stack gives no raw socket */
static long raw_errno;
static PingStat ping_r[2], ping_i[2];   /* router / internet: small, big */
static ULONG internet_ip;

static UWORD csum(const UBYTE *p, int n)
{
  ULONG s = 0;
  int i;
  for (i = 0; i + 1 < n; i += 2) s += ((ULONG)p[i] << 8) | p[i + 1];
  if (n & 1) s += (ULONG)p[n - 1] << 8;
  while (s >> 16) s = (s & 0xffffUL) + (s >> 16);
  return (UWORD)~s;
}

static int loss(const PingStat *p)
{
  return p->sent ? (p->sent - p->got) * 100 / p->sent : 0;
}

static ULONG avg01(const PingStat *p)
{
  return p->got ? p->sum / (ULONG)p->got : 0;
}

/* one reply (or an ICMP error about a request) in buf */
static void ping_reply(const UBYTE *buf, int len, ULONG from, ULONG ip, UWORD id, int size,
                       const ULONG *t_sent, UBYTE *got, ULONG *last, PingStat *ps, ULONG now)
{
  int off = 0, seq;
  if (len > 0 && (buf[0] >> 4) == 4) off = (buf[0] & 15) * 4;    /* the IP header */
  if (len < off + 8) return;
  if (buf[off] == 0) {                                           /* echo reply */
    if (from != ip || ((buf[off + 4] << 8) | buf[off + 5]) != id) return;
    seq = (buf[off + 6] << 8) | buf[off + 7];
    if (seq >= ps->sent || got[seq]) return;
    if (len - off - 8 != size || buf[off + 8 + size - 1] != (UBYTE)(size - 1)) return;
    got[seq] = 1;
    ps->got++;
    {
      ULONG t = ticks_01ms(now - t_sent[seq]);
      if (ps->got == 1 || t < ps->min) ps->min = t;
      if (t > ps->max) ps->max = t;
      ps->sum += t;
      if (*last != 0xffffffffUL) ps->jit += t > *last ? t - *last : *last - t;
      *last = t;
    }
  } else if (buf[off] == 3 || buf[off] == 11) {                  /* unreachable, TTL */
    int o2 = off + 8, ihl;
    if (len < o2 + 1) return;
    ihl = (buf[o2] & 15) * 4;
    if (len < o2 + ihl + 8) return;
    if (((buf[o2 + ihl + 4] << 8) | buf[o2 + ihl + 5]) != id) return;
    if (get32(buf + o2 + 16) != ip) return;
    ps->errors++;
  }
}

/* PING_COUNT requests of size data bytes to ip, one every PING_EVERY_MS,
 * then at most PING_TAIL_MS for the last replies. 0 when no raw socket. */
static int ping_series(ULONG ip, int size, PingStat *ps)
{
  static UBYTE pkt[8 + PING_BIG], buf[1600];
  ULONG t_sent[PING_COUNT], next, end = 0, last = 0xffffffffUL;
  UBYTE got[PING_COUNT];
  struct sockaddr_in to;
  long sock, one = 1;
  UWORD id = (UWORD)(((ULONG)FindTask(NULL) >> 4) ^ (size << 4));
  int i;

  memset(ps, 0, sizeof(*ps));
  memset(got, 0, sizeof(got));
  sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock < 0) {
    raw_failed = 1;
    raw_errno = Errno();
    return 0;
  }
  IoctlSocket(sock, FIONBIO, (APTR)&one);
  memset(&to, 0, sizeof(to));
  to.sin_len = sizeof(to);
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = ip;
  for (i = 0; i < size; i++) pkt[8 + i] = (UBYTE)i;
  ps->ran = 1;
  next = ticks();
  for (;;) {
    ULONG now = ticks(), wait_t;
    fd_set rd;
    struct timeval tv;
    ULONG sigs = SIGBREAKF_CTRL_C;
    long n;
    if (ps->sent < PING_COUNT && (LONG)(now - next) >= 0) {
      int seq = ps->sent;
      pkt[0] = 8;                              /* echo request */
      pkt[1] = 0;
      pkt[2] = pkt[3] = 0;
      pkt[4] = (UBYTE)(id >> 8);
      pkt[5] = (UBYTE)id;
      pkt[6] = (UBYTE)(seq >> 8);
      pkt[7] = (UBYTE)seq;
      {
        UWORD c = csum(pkt, 8 + size);
        pkt[2] = (UBYTE)(c >> 8);
        pkt[3] = (UBYTE)c;
      }
      t_sent[seq] = ticks();
      if (sendto(sock, pkt, 8 + size, 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
        if (seq == 0 && (Errno() == EACCES || Errno() == EPERM || Errno() == EOPNOTSUPP ||
                         Errno() == EPROTONOSUPPORT)) {
          raw_failed = 1;
          raw_errno = Errno();
          ps->ran = 0;
          break;
        }
      }
      ps->sent++;
      next += ms_ticks(PING_EVERY_MS);
      if (ps->sent == PING_COUNT) end = ticks() + ms_ticks(PING_TAIL_MS);
      continue;
    }
    if (ps->sent == PING_COUNT && (ps->got == PING_COUNT || (LONG)(now - end) >= 0)) break;
    wait_t = ps->sent < PING_COUNT ? next - now : end - now;
    if ((LONG)wait_t < 0) wait_t = 0;
    wait_t = ticks_01ms(wait_t) * 100UL;       /* microseconds */
    FD_ZERO(&rd);
    FD_SET(sock, &rd);
    tv.tv_secs = wait_t / 1000000UL;
    tv.tv_micro = wait_t % 1000000UL;
    n = WaitSelect(sock + 1, &rd, NULL, NULL, &tv, &sigs);
    if ((sigs & SIGBREAKF_CTRL_C) || (n < 0 && Errno() == EINTR)) {
      hub_break = 1;
      break;
    }
    if (n > 0) {
      for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        long len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (len <= 0) break;
        ping_reply(buf, (int)len, from.sin_addr.s_addr, ip, id, size, t_sent, got, &last, ps,
                   ticks());
      }
    } else if (n < 0) {
      break;
    }
  }
  if (ps->got > 1) ps->jit /= (ULONG)(ps->got - 1);
  CloseSocket(sock);
  return ps->ran;
}

static void ping_row(const char *who, int size, const PingStat *ps, int router)
{
  char what[60];
  int l = loss(ps), m;
  ULONG a = avg01(ps);
  snprintf(what, sizeof(what), "%s, %d B", who, size);
  if (ps->got == 0) {
    row(FAIL, what, T("%d wyslanych, brak odpowiedzi%s", "%d sent, no answer%s"), ps->sent,
        ps->errors ? T(" (komunikaty bledu ICMP)", " (ICMP error messages)") : "");
    return;
  }
  m = l >= 10 ? FAIL : l > 0 ? SLOW : OK;
  if (m == OK && router && (a > 500 || ps->max > 5000)) m = SLOW;     /* > 50 / 500 ms */
  row(m, what, T("strata %d%%, %lu.%lu/%lu.%lu/%lu.%lu ms, rozrzut %lu.%lu",
                 "loss %d%%, %lu.%lu/%lu.%lu/%lu.%lu ms, jitter %lu.%lu"), l,
      (unsigned long)(ps->min / 10), (unsigned long)(ps->min % 10), (unsigned long)(a / 10),
      (unsigned long)(a % 10), (unsigned long)(ps->max / 10), (unsigned long)(ps->max % 10),
      (unsigned long)(ps->jit / 10), (unsigned long)(ps->jit % 10));
}

static void test_ping(void)
{
  char a[20];
  if (st.router) {
    ip_text(st.router, a, sizeof(a));
    if (ping_series(st.router, PING_SMALL, &ping_r[0]) && !stopped()) {
      ping_row(T("router", "router"), PING_SMALL, &ping_r[0], 1);
      if (ping_series(st.router, PING_BIG, &ping_r[1])) {
        ping_row(T("router", "router"), PING_BIG, &ping_r[1], 1);
      }
    }
  }
  if (!raw_failed && !stopped()) {
    internet_ip = (1UL << 24) | (1UL << 16) | (1UL << 8) | 1UL;   /* 1.1.1.1 */
    if (ping_series(internet_ip, PING_SMALL, &ping_i[0]) && !stopped()) {
      ping_row("internet 1.1.1.1", PING_SMALL, &ping_i[0], 0);
      if (ping_series(internet_ip, PING_BIG, &ping_i[1])) {
        ping_row("internet 1.1.1.1", PING_BIG, &ping_i[1], 0);
      }
    }
  }
  if (raw_failed) {
    row(INFO, "ping", T("stos TCP/IP nie daje gniazda ICMP (errno %ld) - pominiety",
                        "the TCP/IP stack gives no ICMP socket (errno %ld) - skipped"), raw_errno);
  }
  row(INFO, "", T("(min/sr./max; rozrzut = zmiana czasu miedzy pingami)",
                  "(min/avg/max; jitter = change of the time between pings)"));
}

/* --- certificates, TLS, ports ------------------------------------------- */

static struct {
  int good_ok;             /* badssl.com connected */
  int good_err;            /* badssl.com refused / failed */
  int bad_passed;          /* bad certificates that were taken */
  int not_checked;         /* bad ones that failed for another reason (network) */
  char passed[120];
  char good_why[160];
  char tls[40], rating[40];
  int port_ok[3], port_tried;
  long port_ms[3];
  int priv_port_ok;        /* the port of the private source at portquiz (-1 not tried) */
  int priv_port;
} sec;

static int null_sink(void *user, const UBYTE *data, LONG len)
{
  (void)user; (void)data; (void)len;
  return !stopped();
}

/* 1 connected (HTTP answer), 0 certificate refused, -1 other error (err) */
static int try_https(const char *host)
{
  HubConn *k = hub_conn_new(host, 443);
  int status;
  if (k == NULL) return -1;
  status = hub_get(k, "/", null_sink, NULL, NULL, 0, err, sizeof(err));
  hub_conn_free(k);
  if (status >= 0) return 1;
  return strncmp(err, "Certificate", 11) == 0 ? 0 : -1;
}

static void test_certs(void)
{
  static const char *bad[3] = { "expired.badssl.com", "self-signed.badssl.com",
                                "wrong.host.badssl.com" };
  int i, k;
  k = try_https("badssl.com");
  if (k == 1) {
    sec.good_ok = 1;
    row(OK, "badssl.com", T("dobry certyfikat - polaczono", "good certificate - connected"));
  } else {
    sec.good_err = 1;
    snprintf(sec.good_why, sizeof(sec.good_why), "%s", err);
    row(FAIL, "badssl.com", "%.120s", err);
  }
  for (i = 0; i < 3 && !stopped(); i++) {
    k = try_https(bad[i]);
    if (k == 0) {
      const char *why = strstr(err, "refused: ");
      row(OK, bad[i], T("odrzucony (%.60s)", "refused (%.60s)"), why != NULL ? why + 9 : err);
    } else if (k == 1) {
      sec.bad_passed++;
      snprintf(sec.passed + strlen(sec.passed), sizeof(sec.passed) - strlen(sec.passed), "%s%s",
               sec.passed[0] ? ", " : "", bad[i]);
      row(FAIL, bad[i], T("PRZYJETY - zly certyfikat przeszedl!", "TAKEN - a bad certificate got through!"));
    } else {
      sec.not_checked++;
      row(SLOW, bad[i], T("nie sprawdzono: %.80s", "not checked: %.80s"), err);
    }
  }
  if (!stopped()) {
    if (sec.bad_passed) {
      row(FAIL, T("certyfikaty", "certificates"), T("AmiSSL nie sprawdza certyfikatow",
                                                      "AmiSSL does not check the certificates"));
    } else if (sec.good_ok && sec.not_checked == 0) {
      row(OK, T("certyfikaty", "certificates"), T("sprawdzane", "checked"));
    }
  }
}

/* "key":"value" of a JSON text */
static void json_str(const char *text, const char *key, char *out, int outlen)
{
  char pat[40];
  const char *p;
  int n = 0;
  out[0] = 0;
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  p = strstr(text, pat);
  if (p == NULL) return;
  p += strlen(pat);
  while (*p && *p != '"' && n < outlen - 1) out[n++] = *p++;
  out[n] = 0;
}

static void test_tls(void)
{
  HubConn *k = hub_conn_new("www.howsmyssl.com", 443);
  Sink s;
  ULONG ms;
  int status;
  if (k == NULL) return;
  memset(&s, 0, sizeof(s));
  s.keep_cap = 8192;
  s.keep = (char *)hub_malloc(s.keep_cap + 1);
  status = get(k, "/a/check", &s, &ms);
  if (status == 200 && s.keep != NULL) {
    s.keep[s.got < s.keep_cap ? s.got : s.keep_cap] = 0;
    json_str(s.keep, "tls_version", sec.tls, sizeof(sec.tls));
    json_str(s.keep, "rating", sec.rating, sizeof(sec.rating));
    row(strcmp(sec.rating, "Bad") == 0 ? SLOW : INFO, "howsmyssl.com", T("%s, ocena: %s", "%s, rating: %s"),
        sec.tls[0] ? sec.tls : "?", sec.rating[0] ? sec.rating : "?");
  } else {
    row(INFO, "howsmyssl.com", T("bez odpowiedzi: %.100s", "no answer: %.100s"),
        status < 0 ? err : "HTTP error");
  }
  hub_free(s.keep);
  hub_conn_free(k);
}

static void test_ports(int priv_port)
{
  static const int ports[3] = { 80, 443, 6831 };
  int i;
  sec.port_tried = 1;
  sec.priv_port_ok = -1;
  sec.priv_port = priv_port;
  for (i = 0; i < 3 && !stopped(); i++) {
    sec.port_ms[i] = test_tcp("portquiz.net", ports[i], 5);
    sec.port_ok[i] = sec.port_ms[i] >= 0;
  }
  if (priv_port > 0 && priv_port != 6831 && priv_port != 80 && priv_port != 443 && !stopped()) {
    sec.priv_port_ok = test_tcp("portquiz.net", priv_port, 5) >= 0;
  }
}

/* the summary of the ping, the certificates and the ports */
static void verdict(int m, const char *text);

static void summary_ping(void)
{
  char a[300];
  int ls = loss(&ping_r[0]), lb = loss(&ping_r[1]);
  int is = loss(&ping_i[0]), ib = loss(&ping_i[1]);
  int router_ok = 0;
  if (raw_failed) {
    snprintf(a, sizeof(a), T("Ping niedostepny w tym stosie TCP/IP (errno %ld) - odcinek Amiga-router nie sprawdzony.",
                             "Ping is not available in this TCP/IP stack (errno %ld) - Amiga-router not checked."),
             raw_errno);
    verdict(INFO, a);
    return;
  }
  if (!ping_r[0].ran && !ping_i[0].ran) return;
  if (st.router == 0) {
    verdict(INFO, T("Router (brama) nieznany - odcinek Amiga-router nie sprawdzony (opcja ROUTER=<adres>).",
                    "Router (gateway) not known - Amiga-router not checked (option ROUTER=<address>)."));
  } else if (ping_r[0].ran && ping_r[0].got == 0 && ping_r[1].got == 0) {
    if (ping_i[0].got > 0) {
      verdict(INFO, T("Router nie odpowiada na ping (czesto ma to wylaczone) - odcinka Amiga-router nie da sie ocenic.",
                      "The router does not answer pings (often switched off) - Amiga-router cannot be judged."));
    } else if (r.tcp443) {
      verdict(INFO, T("Nikt nie odpowiada na ping, ale TCP dziala - siec blokuje ping, nie da sie ocenic.",
                      "Nobody answers pings, but TCP works - the network blocks pings, cannot be judged."));
    } else {
      verdict(FAIL, T("Brak odpowiedzi na ping z routera i z internetu: Amiga nie ma polaczenia z siecia.",
                      "No answer to pings from the router nor the internet: the Amiga has no network."));
      verdict(INFO, T("Sprawdz WiFi (WarpTool / WarpDiag) albo kabel i router.",
                      "Check the WiFi (WarpTool / WarpDiag) or the cable and the router."));
    }
  } else if (ping_r[0].ran) {
    if (ls >= 10) {
      snprintf(a, sizeof(a), T("Polaczenie Amiga-router gubi %d%% pakietow (duze %d%%) - problem w WiFi,",
                               "The Amiga-router link loses %d%% of the packets (big %d%%) - a problem of the"),
               ls, lb);
      verdict(FAIL, a);
      verdict(INFO, T("kablu albo routerze, nie w internecie. Sprobuj: inny router (np. telefon jako",
                      "WiFi, the cable or the router, not of the internet. Try: another router (e.g. a"));
      verdict(INFO, T("hotspot), inny kanal WiFi, Amige blizej routera, inny kabel.",
                      "phone as a hotspot), another WiFi channel, the Amiga nearer, another cable."));
    } else if (lb >= 10 && ls < 5) {
      snprintf(a, sizeof(a), T("Duze pakiety do routera gina (%d%%), male nie (%d%%) - problem MTU / WiFi.",
                               "Big packets to the router get lost (%d%%), small not (%d%%) - MTU / WiFi."),
               lb, ls);
      verdict(FAIL, a);
      verdict(INFO, T("Sprobuj mniejsze MTU (np. mtu=1400 w ustawieniach interfejsu) albo inny router / kanal.",
                      "Try a smaller MTU (e.g. mtu=1400 in the interface settings) or another router / channel."));
    } else if (ls > 0 || lb > 0) {
      snprintf(a, sizeof(a), T("Polaczenie Amiga-router: male straty (%d%%, duze %d%%). Przy klopotach sprawdz WiFi.",
                               "Amiga-router link: small losses (%d%%, big %d%%). With trouble check the WiFi."),
               ls, lb);
      verdict(SLOW, a);
      router_ok = 1;
    } else if (avg01(&ping_r[0]) > 500 || ping_r[0].max > 5000 || ping_r[1].max > 5000) {
      snprintf(a, sizeof(a), T("Router odpowiada wolno / nierowno (sr. %lu ms, max %lu ms) - WiFi slabe albo zaklocone.",
                               "The router answers slowly / unevenly (avg %lu ms, max %lu ms) - weak or disturbed WiFi."),
               (unsigned long)(avg01(&ping_r[0]) / 10),
               (unsigned long)((ping_r[0].max > ping_r[1].max ? ping_r[0].max : ping_r[1].max) / 10));
      verdict(SLOW, a);
      router_ok = 1;
    } else {
      snprintf(a, sizeof(a), T("Polaczenie Amiga-router: bez strat (sr. %lu.%lu ms).",
                               "Amiga-router link: no losses (avg %lu.%lu ms)."),
               (unsigned long)(avg01(&ping_r[0]) / 10), (unsigned long)(avg01(&ping_r[0]) % 10));
      verdict(OK, a);
      router_ok = 1;
    }
  }
  if (!ping_i[0].ran) return;
  if (ping_i[0].got == 0 && ping_i[1].got == 0) {
    if (ping_r[0].got > 0) {
      verdict(INFO, T("1.1.1.1 nie odpowiada na ping (siec za routerem blokuje ping?) - internet nie oceniony.",
                      "1.1.1.1 does not answer pings (blocked behind the router?) - internet not judged."));
    }
  } else if (router_ok || st.router == 0 || ping_r[0].got == 0) {
    if (is >= 10 || (ib >= 10 && is >= 5)) {
      snprintf(a, sizeof(a), st.router && router_ok
               ? T("Router OK, ale internet gubi %d%% (duze %d%%) - problem za routerem / u dostawcy.",
                   "The router is fine, but the internet loses %d%% (big %d%%) - behind the router / the provider.")
               : T("Internet (1.1.1.1) gubi %d%% (duze %d%%) - WiFi, router albo dostawca.",
                   "The internet (1.1.1.1) loses %d%% (big %d%%) - WiFi, router or the provider."), is, ib);
      verdict(FAIL, a);
    } else if (ib >= 10) {
      snprintf(a, sizeof(a), T("Duze pakiety do internetu gina (%d%%), male nie - MTU (np. lacze LTE / PPPoE):",
                               "Big packets to the internet get lost (%d%%), small not - MTU (e.g. LTE / PPPoE):"), ib);
      verdict(FAIL, a);
      verdict(INFO, T("ustaw mniejsze MTU, np. mtu=1400.", "set a smaller MTU, e.g. mtu=1400."));
    } else if (is > 0 || ib > 0) {
      snprintf(a, sizeof(a), T("Internet: male straty (%d%%, duze %d%%).", "Internet: small losses (%d%%, big %d%%)."),
               is, ib);
      verdict(SLOW, a);
    } else {
      snprintf(a, sizeof(a), T("Internet (1.1.1.1): bez strat (sr. %lu ms).", "Internet (1.1.1.1): no losses (avg %lu ms)."),
               (unsigned long)(avg01(&ping_i[0]) / 10));
      verdict(OK, a);
    }
  }
}

static void summary_certs(void)
{
  char a[300];
  if (sec.bad_passed) {
    snprintf(a, sizeof(a), T("AmiSSL nie sprawdza certyfikatow: przyjal %.120s.",
                             "AmiSSL does not check the certificates: it took %.120s."), sec.passed);
    verdict(FAIL, a);
    verdict(INFO, T("Ktos w sieci moglby podmienic pobierane pliki. Sprawdz date Amigi i wersje",
                    "Someone in the network could swap the downloaded files. Check the date and the"));
    verdict(INFO, T("AmiSSL (5.x, aktualna), na Warpie takze oprogramowanie karty (WarpSSL).",
                    "version of AmiSSL (5.x, current), on a Warp also the firmware (WarpSSL)."));
  } else if (sec.good_err && strncmp(sec.good_why, "Certificate", 11) == 0) {
    snprintf(a, sizeof(a), T("Dobry certyfikat odrzucony (%.120s) - zla data albo stare AmiSSL.",
                             "A good certificate refused (%.120s) - wrong date or an old AmiSSL."), sec.good_why);
    verdict(FAIL, a);
  }
  if (sec.rating[0] && strcmp(sec.rating, "Bad") == 0) {
    snprintf(a, sizeof(a), T("Szyfrowanie oceniane jako slabe (%s) - zaktualizuj AmiSSL.",
                             "Encryption rated weak (%s) - update AmiSSL."), sec.tls);
    verdict(SLOW, a);
  }
}

static void summary_ports(int priv_tcp_failed)
{
  static const int ports[3] = { 80, 443, 6831 };
  char a[300];
  int i, any = 0;
  if (!sec.port_tried) return;
  for (i = 0; i < 3; i++) any |= sec.port_ok[i];
  if (!any) {
    verdict(INFO, T("portquiz.net nie odpowiada na zadnym porcie - porty nie sprawdzone.",
                    "portquiz.net answers on no port - the ports not checked."));
    return;
  }
  for (i = 0; i < 3; i++) {
    if (sec.port_ok[i]) continue;
    if (ports[i] == 6831) {
      verdict(FAIL, T("Port 6831 zablokowany w Twojej sieci - prywatne zrodlo WARP HUB nie zadziala stad.",
                      "Port 6831 is blocked in your network - the private source of WARP HUB cannot work from here."));
    } else {
      snprintf(a, sizeof(a), T("Port %d zablokowany w Twojej sieci (zapora / router).",
                               "Port %d is blocked in your network (firewall / router)."), ports[i]);
      verdict(FAIL, a);
    }
  }
  if (sec.priv_port_ok == 0) {
    snprintf(a, sizeof(a), T("Port %d (prywatne zrodlo) zablokowany w Twojej sieci.",
                             "Port %d (private source) is blocked in your network."), sec.priv_port);
    verdict(FAIL, a);
  }
  if (priv_tcp_failed && (sec.priv_port == 6831 ? sec.port_ok[2] : sec.priv_port_ok == 1)) {
    verdict(INFO, T("Port prywatnego serwera jest otwarty w Twojej sieci - to serwer nie odpowiada.",
                    "The port of the private server is open in your network - the server does not answer."));
  }
}

/* --- the time ---------------------------------------------------------------- */

static void time_text(ULONG t, char *out, int outlen)
{
  long z = (long)(t / 86400UL) + 719468L;
  long era = z / 146097L, doe = z - era * 146097L;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1;
  long m = mp < 10 ? mp + 3 : mp - 9;
  ULONG s = t % 86400UL;
  snprintf(out, outlen, "%04ld-%02ld-%02ld %02lu:%02lu:%02lu", y + (m <= 2), m, d,
           (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}

static ULONG test_time(void)
{
  char a[40];
  ULONG now = hub_amiga_now(), t, h = 0, t0;
  time_text(now, a, sizeof(a));
  row(INFO, T("zegar Amigi", "clock of the Amiga"), "%s", a);
  t0 = hub_ms();
  t = hub_sntp("pool.ntp.org", err, sizeof(err));
  if (t == 0) t = hub_sntp("time.cloudflare.com", err, sizeof(err));
  if (t) {
    r.ntp_ok = 1;
    time_text(t, a, sizeof(a));
    row(OK, T("serwer czasu (NTP, UDP 123)", "time server (NTP, UDP 123)"), "%s UTC", a);
  } else {
    row(FAIL, T("serwer czasu (NTP, UDP 123)", "time server (NTP, UDP 123)"),
        T("brak odpowiedzi (%lu ms)", "no answer (%lu ms)"), (unsigned long)(hub_ms() - t0));
  }
  if (!stopped()) {
    h = hub_http_time(RAW, err, sizeof(err));
    if (h) {
      r.http_time_ok = 1;
      time_text(h, a, sizeof(a));
      row(OK, T("czas z HTTP (port 80)", "time of HTTP (port 80)"), "%s UTC", a);
    } else {
      row(FAIL, T("czas z HTTP (port 80)", "time of HTTP (port 80)"), "%s", err);
    }
  }
  if (t == 0) t = h;
  if (t != 0) {
    r.have_time = 1;
    r.clock_off = (long)(now - t);
  }
  return t;
}

/* --- the summary --------------------------------------------------------------- */

static int problems;

static void verdict(int m, const char *text)
{
  char line[300];
  int first = 1;
  if (m == SLOW || m == FAIL) problems++;
  /* lines of at most 70 characters after the mark (the window has 78) */
  while (*text) {
    int n = (int)strlen(text), cut = n;
    if (n > 70) {
      for (cut = 70; cut > 20 && text[cut] != ' '; cut--) {
      }
      if (text[cut] != ' ') cut = 70;
    }
    snprintf(line, sizeof(line), " %-7s %.*s", first ? mark(m) : "", cut, text);
    put(line, m == FAIL);
    text += cut;
    while (*text == ' ') text++;
    first = 0;
  }
}

static void summary(HubPrefs *p)
{
  char a[300];
  head(T("=== WYNIK ===", "=== RESULT ==="));
  if (!r.dns_ok) {
    verdict(FAIL, T("Amiga nie znajduje adresu GitHuba (DNS). Sprawdz ustawienia sieci",
                    "The Amiga cannot find GitHub (DNS). Check the network settings"));
    verdict(INFO, T("(serwer DNS w stosie TCP/IP, np. adres routera albo 8.8.8.8).",
                    "(DNS server of the TCP/IP stack, e.g. the router or 8.8.8.8)."));
  }
  summary_ping();
  if ((r.tcp443 && r.tcp443_ms > 1000) || (r.priv_tcp && r.priv_tcp_ms > 1000)) {
    snprintf(a, sizeof(a), T("Bardzo wolne laczenie (%ld ms zamiast kilkudziesieciu): siec gubi pakiety.",
                             "Very slow connecting (%ld ms instead of tens): the network loses packets."),
             r.tcp443_ms > 1000 ? (long)r.tcp443_ms : r.priv_tcp_ms);
    verdict(FAIL, a);
  }
  if (r.gh_timeout || r.priv_timeout) {
    verdict(FAIL, T("Polaczenie sie otwiera, ale dane nie dochodza (limit czasu).",
                    "The connection opens, but no data comes (time limit)."));
  }
  if ((r.tcp443 && r.tcp443_ms > 1000) || (r.priv_tcp && r.priv_tcp_ms > 1000) || r.gh_timeout ||
      r.priv_timeout || !r.dns_ok) {
    verdict(INFO, T("Sprawdz: kabel i port switcha, tryb karty (full/half duplex), most Wi-Fi;",
                    "Check: the cable and the switch port, the mode of the card (full/half duplex),"));
    verdict(INFO, T("w Shellu ping -s 100 8.8.8.8 i ping -s 1400 8.8.8.8 - gdy duzy nie",
                    "a Wi-Fi bridge; in the Shell ping -s 100 8.8.8.8 and ping -s 1400 8.8.8.8 -"));
    verdict(INFO, T("przechodzi, ustaw mniejsze MTU karty (np. mtu=1400 w Roadshow).",
                    "when the big one fails, set a smaller MTU of the card (e.g. mtu=1400)."));
  }
  if (r.tcp443 == 0) {
    verdict(FAIL, T("Brak polaczenia z GitHubem (port 443). Zapora albo siec blokuje HTTPS.",
                    "No connection to GitHub (port 443). A firewall or the network blocks HTTPS."));
  }
  if (!r.ntp_ok && r.http_time_ok) {
    verdict(SLOW, T("Serwer czasu (NTP) nie odpowiada - router blokuje port UDP 123. WARP HUB",
                    "The time server (NTP) does not answer - port UDP 123 is blocked. WARP HUB"));
    verdict(INFO, T("od 0.13 bierze czas z HTTP; starsze wersje wymagaja dobrej daty.",
                    "0.13 and newer take the time of HTTP; older ones need a right date."));
  } else if (!r.ntp_ok && !r.http_time_ok) {
    verdict(FAIL, T("Brak czasu z sieci: ustaw date i godzine w Prefs/Time.",
                    "No time from the network: set the date and time in Prefs/Time."));
  }
  if (r.have_time && (r.clock_off > 2L * 86400L || r.clock_off < -2L * 86400L)) {
    verdict(SLOW, T("Zegar Amigi jest zle ustawiony (o ponad 2 dni). Warto ustawic date.",
                    "The clock of the Amiga is wrong (more than 2 days). Set the date."));
  } else if (!r.have_time && !HUB_CLOCK_OK(hub_amiga_now())) {
    verdict(SLOW, T("Zegar Amigi jest zle ustawiony (bateria zegara?). Ustaw date w Prefs/Time.",
                    "The clock of the Amiga is wrong (clock battery?). Set the date in Prefs/Time."));
  }
  if (!r.gh_ok && r.tcp443) {
    snprintf(a, sizeof(a), T("Polaczenie szyfrowane z GitHubem sie nie udaje: %.150s",
                             "The encrypted connection to GitHub fails: %.150s"), r.gh_err);
    verdict(FAIL, a);
    verdict(INFO, T("Sprawdz AmiSSL (wersja 5) i date Amigi.", "Check AmiSSL (version 5) and the date."));
  }
  if (r.gh_ok) {
    ULONG k = r.gh_zip_kbs ? r.gh_zip_kbs : r.gh_cat_kbs;
    int games = r.gh_games > 0 ? r.gh_games : 10;
    /* first start: ~4 files a game (~38 KB a game) + the catalog; each file
     * also ~220 ms in WARP HUB (written to the cache, checked): measured on
     * Amiga 1, 43 files in 11.4 s with a small request of ~40 ms */
    ULONG est = r.gh_first_ms / 1000UL + (ULONG)games * 4UL * (r.gh_small_ms + 220UL) / 1000UL +
                (k ? ((ULONG)games * 38UL + 80UL) / k : 0);
    if (k >= 100 && r.gh_small_ms <= 300 && r.gh_first_ms <= 3000) {
      snprintf(a, sizeof(a), T("GitHub: szybko (%lu KB/s). WARP HUB dziala bez problemu.",
                               "GitHub: fast (%lu KB/s). WARP HUB works without trouble."), (unsigned long)k);
      verdict(OK, a);
    } else if (k >= 30) {
      snprintf(a, sizeof(a), T("GitHub: wolniej (%lu KB/s, male zapytanie %lu ms), ale dziala.",
                               "GitHub: slower (%lu KB/s, small request %lu ms), but it works."),
               (unsigned long)k, (unsigned long)r.gh_small_ms);
      verdict(SLOW, a);
    } else {
      snprintf(a, sizeof(a), T("GitHub: bardzo wolno (%lu KB/s). Lacze albo siec Amigi gubi dane.",
                               "GitHub: very slow (%lu KB/s). The line or the network of the Amiga loses data."),
               (unsigned long)k);
      verdict(FAIL, a);
    }
    snprintf(a, sizeof(a), T("Pierwszy start WARP HUB (obrazki i opisy %d gier): ok. %lu s;",
                             "First start of WARP HUB (pictures and texts of %d games): about %lu s;"),
             games, (unsigned long)(est < 1 ? 1 : est));
    verdict(INFO, a);
    snprintf(a, sizeof(a), T("pobranie gry 10 MB: ok. %lu s.", "downloading a 10 MB game: about %lu s."),
             (unsigned long)(k ? 10240UL / k : 0));
    verdict(INFO, a);
  }
  summary_certs();
  summary_ports(p->priv[0] && !r.priv_tcp);
  if (p->priv[0]) {
    if (!r.priv_tcp) {
      verdict(FAIL, T("Prywatny serwer: brak polaczenia (adres, port albo serwer wylaczony).",
                      "Private server: no connection (address, port or the server is off)."));
    } else if (!r.priv_ok) {
      snprintf(a, sizeof(a), T("Prywatny serwer: polaczenie szyfrowane nie dziala: %.130s",
                               "Private server: the encrypted connection fails: %.130s"), r.priv_err);
      verdict(FAIL, a);
    } else if (r.priv_status == 200) {
      snprintf(a, sizeof(a), T("Prywatny serwer: dziala (%lu KB/s).", "Private server: works (%lu KB/s)."),
               (unsigned long)r.priv_kbs);
      verdict(speed_mark(r.priv_kbs) == FAIL ? FAIL : speed_mark(r.priv_kbs) == SLOW ? SLOW : OK, a);
    } else {
      verdict(OK, T("Prywatny serwer: odpowiada (bez zapamietanego hasla - bez pomiaru).",
                    "Private server: answers (no remembered password - not measured)."));
    }
  }
  if (problems == 0) {
    verdict(OK, T("Wszystko w porzadku.", "Everything is fine."));
  }
}

/* --- main ------------------------------------------------------------------ */

struct Library *IconBase;

/* ROUTER=<address>: Shell argument or Tool Type of the icon */
static void read_options(char *router, int len)
{
  router[0] = 0;
  if (_WBenchMsg != NULL) {
    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 37);
    if (IconBase != NULL) {
      BPTR old = CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);
      struct DiskObject *dobj = GetDiskObject((STRPTR)_WBenchMsg->sm_ArgList[0].wa_Name);
      if (dobj != NULL) {
        STRPTR v = FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"ROUTER");
        if (v != NULL) snprintf(router, len, "%s", (char *)v);
        FreeDiskObject(dobj);
      }
      CurrentDir(old);
      CloseLibrary(IconBase);
      IconBase = NULL;
    }
  } else {
    LONG args[1] = {0};
    struct RDArgs *rd = ReadArgs((CONST_STRPTR)"ROUTER/K", args, NULL);
    if (rd != NULL) {
      if (args[0]) snprintf(router, len, "%s", (char *)args[0]);
      FreeArgs(rd);
    }
  }
}

/* the port of the private source ("host:port", 6831 without one) */
static int priv_port_of(const HubPrefs *p)
{
  const char *colon = strrchr(p->priv, ':');
  if (p->priv[0] == 0) return 0;
  return colon != NULL ? atoi(colon + 1) : 6831;
}

int main(void)
{
  static HubPrefs prefs;
  char router_opt[64];
  ULONG start;
  UWORD attn = SysBase->AttnFlags;
  long ms;
  (void)vers;

  memset(&prefs, 0, sizeof(prefs));
  read_options(router_opt, sizeof(router_opt));
  hub_prefs_load(&prefs, "PROGDIR:WarpHub.prefs");
  if (prefs.priv[0] == 0 && prefs.language[0] == 0) hub_prefs_load(&prefs, "SYS:WarpApps/WarpHub.prefs");
  pl = prefs.language[0] == 0 || strcmp(prefs.language, "pl") == 0;

  if (_WBenchMsg != NULL) {
    out_fh = Open((CONST_STRPTR)"CON:20/20/640/440/WARP HUB net test/CLOSE", MODE_NEWFILE);
    own_window = out_fh != 0;
  } else {
    out_fh = Output();
  }
  logcap = 32768;
  logbuf = (char *)hub_malloc(logcap);
  start = hub_ms();

  put(T("WarpNetTest " NETTEST_VERSION " - test polaczenia WARP HUB",
        "WarpNetTest " NETTEST_VERSION " - connection test of WARP HUB"), 1);
  say("CPU 680%s0, Kickstart %u.%u", (attn & AFF_68060) ? "6" : (attn & AFF_68040) ? "4" :
      (attn & AFF_68030) ? "3" : (attn & AFF_68020) ? "2" : "0",
      SysBase->LibNode.lib_Version, SysBase->SoftVer);

  if (!hub_https_init(NULL, err, sizeof(err))) {
    head(T("=== WYNIK ===", "=== RESULT ==="));
    verdict(FAIL, err);
    verdict(INFO, T("Brak stosu TCP/IP albo AmiSSL 5 - WARP HUB nie zadziala.",
                    "No TCP/IP stack or AmiSSL 5 - WARP HUB cannot work."));
    goto out;
  }
  timer_open();
  say("AmiSSL: %s", hub_https_version());
  say(T("(trwa zwykle 20-40 sekund; Ctrl-C przerywa)",
        "(takes usually 20-40 seconds; Ctrl-C stops)"));

  head(T("1. Siec: stos, nazwy i polaczenia", "1. Network: stack, names and connections"));
  test_stack(router_opt);
  r.dns_ok = test_dns(RAW);
  if (r.dns_ok && !stopped()) {
    ms = test_tcp(RAW, 443, 10);
    r.tcp443 = ms >= 0;
    r.tcp443_ms = ms >= 0 ? (ULONG)ms : 0;
  }
  if (r.dns_ok && !stopped()) r.tcp80 = test_tcp(RAW, 80, 10) >= 0;
  save_log();

  if (!stopped()) {
    head(T("2. Ping: Amiga - router - internet (po 20 pakietow)",
           "2. Ping: Amiga - router - internet (20 packets each)"));
    test_ping();
  }
  save_log();

  if (!stopped()) {
    ULONG t;
    head(T("3. Czas (do sprawdzania certyfikatow)", "2. Time (for the certificate check)"));
    t = test_time();
    if (t != 0) {
      hub_https_verify_time(t);
    }
  }
  save_log();

  if (r.tcp443 && !stopped() && (r.have_time || HUB_CLOCK_OK(hub_amiga_now()))) {
    head("4. GitHub (" RAW ")");
    test_github();
  }
  save_log();

  if (r.dns_ok && !stopped() && (r.have_time || HUB_CLOCK_OK(hub_amiga_now()))) {
    head(T("5. Bezpieczenstwo: certyfikaty i TLS", "5. Security: certificates and TLS"));
    test_certs();
    if (!stopped()) test_tls();
  }
  save_log();

  if (r.dns_ok && !stopped()) {
    head(T("6. Porty (portquiz.net slucha na kazdym porcie)", "6. Ports (portquiz.net listens on every port)"));
    test_ports(priv_port_of(&prefs));
  }
  save_log();

  if (prefs.priv[0] && !stopped()) {
    head(T("7. Prywatny serwer (z WarpHub.prefs)", "4. Private server (of WarpHub.prefs)"));
    test_private(&prefs);
  }
  save_log();

  if (stopped()) {
    head(T("Przerwano (Ctrl-C).", "Stopped (Ctrl-C)."));
  } else {
    summary(&prefs);
  }
  put("", 0);
  say(T("Czas testu: %lu s. Wyniki zapisane w " LOG_FILE " - ten plik prosimy odeslac.",
        "Test time: %lu s. The results are in " LOG_FILE " - please send this file."),
      (unsigned long)((hub_ms() - start) / 1000));
out:
  save_log();
  timer_close();
  hub_https_cleanup();
  memset(&prefs, 0, sizeof(prefs));
  hub_free(logbuf);
  if (own_window) {
    char c;
    put(T("Return albo zamkniecie okna konczy program.", "Return or closing the window ends."), 0);
    Read(out_fh, &c, 1);
    Close(out_fh);
  }
  return RETURN_OK;
}
