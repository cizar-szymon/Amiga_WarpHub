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
#include <dos/dos.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/filio.h>
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

#define NETTEST_VERSION "0.3"
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

/* TCP connect only (no TLS), at most 10 s; ms or -1 */
static long test_tcp(const char *host, int port)
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
    tv.tv_secs = 10;
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
  r.priv_tcp_ms = test_tcp(host, port);
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
  if (m == SLOW || m == FAIL) problems++;
  snprintf(line, sizeof(line), " %-7s %s", mark(m), text);
  put(line, m == FAIL);
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

int main(void)
{
  HubPrefs prefs;
  ULONG start;
  UWORD attn = SysBase->AttnFlags;
  long ms;
  (void)vers;

  memset(&prefs, 0, sizeof(prefs));
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
  say("AmiSSL: %s", hub_https_version());
  say(T("(trwa zwykle kilka-kilkadziesiat sekund; Ctrl-C przerywa)",
        "(takes a few seconds up to a minute; Ctrl-C stops)"));

  head(T("1. Siec: nazwy i polaczenia", "1. Network: names and connections"));
  r.dns_ok = test_dns(RAW);
  if (r.dns_ok && !stopped()) {
    ms = test_tcp(RAW, 443);
    r.tcp443 = ms >= 0;
    r.tcp443_ms = ms >= 0 ? (ULONG)ms : 0;
  }
  if (r.dns_ok && !stopped()) r.tcp80 = test_tcp(RAW, 80) >= 0;
  save_log();

  if (!stopped()) {
    ULONG t;
    head(T("2. Czas (do sprawdzania certyfikatow)", "2. Time (for the certificate check)"));
    t = test_time();
    if (t != 0) {
      hub_https_verify_time(t);
    }
  }
  save_log();

  if (r.tcp443 && !stopped() && (r.have_time || HUB_CLOCK_OK(hub_amiga_now()))) {
    head("3. GitHub (" RAW ")");
    test_github();
  }
  save_log();

  if (prefs.priv[0] && !stopped()) {
    head(T("4. Prywatny serwer (z WarpHub.prefs)", "4. Private server (of WarpHub.prefs)"));
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
