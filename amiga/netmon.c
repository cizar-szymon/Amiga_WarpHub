/* WarpNetMonitor - the speed of the connections of this Amiga over a few
 * minutes, drawn: GitHub (where WARP HUB gets games), two servers made for
 * speed tests (Cloudflare, Hetzner) and the private source of WarpHub.prefs.
 *
 * The targets take turns in slices of 10 s (so each one is seen over the whole
 * test); in a slice the target is downloaded without a break. Every second is
 * a bar of the speed chart (the colour of the target), every request a point
 * of the response chart (time to the first byte, a red mark for an error).
 * Per target: average / min / max KB/s, stalls (more than 1 s without data),
 * errors, response time; at the end a verdict in plain words. Every second
 * and the summary also go to RAM:WarpNetMonitor.txt.
 *
 *   WarpNetMonitor [DURATION=<s>]     (Tool Type DURATION=<s>; default 180)
 * Polish or English after LANGUAGE in WarpHub.prefs (PROGDIR: or
 * SYS:WarpApps), which also gives the private source. No address in the code
 * but the public test servers. Close gadget or Ctrl-C stops. */

#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/icon.h>

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

#define NETMON_VERSION "0.1"
static const char vers[] = "$VER: WarpNetMonitor " NETMON_VERSION " (25.11.2026)";

unsigned long __stack = 65536;
extern void __stkinit(void);
void (*const netmon_stackSwap)(void) = __stkinit;

extern struct WBStartup *_WBenchMsg;
extern struct ExecBase *SysBase;

#define LOG_FILE "RAM:WarpNetMonitor.txt"
#define SLICE_MS 10000UL
#define MAXSEC 900                    /* 15 min at most */
#define MAXREQ 2000

static int pl;
#define T(p, e) (pl ? (p) : (e))

/* --- the targets ----------------------------------------------------------------- */

typedef struct Target {
  const char *name;
  char host[128];
  int port;
  char path[160];
  int auth;                  /* the private source: pin + password */
  int on;
  UBYTE rgb[3];
  LONG pen;
  HubConn *c;
  /* results */
  ULONG bytes;               /* all its bytes */
  ULONG cbytes, secs;        /* of the whole seconds (not the first of a slice) */
  ULONG kmin, kmax;
  int stalls, errors, reqs;
  ULONG ttfb_sum;
  char last_err[160];
} Target;

static Target tg[4] = {
  {"GitHub", "raw.githubusercontent.com", 443,
   "/cizar-szymon/Amiga_game_Settlers/amiga-port/releases/amiga/Settlers-0.140.zip", 0, 1,
   {0x30, 0x70, 0xf0}},
  {"Cloudflare", "speed.cloudflare.com", 443, "/__down?bytes=25000000", 0, 1, {0xf0, 0x90, 0x20}},
  {"Hetzner", "fsn1-speed.hetzner.com", 443, "/100MB.bin", 0, 1, {0x20, 0xb0, 0x50}},
  {"RP4", "", 6831, "/catalog.txt", 1, 0, {0xc0, 0x40, 0xd0}},
};
#define NT 4

static ULONG sec_bytes[MAXSEC];       /* bytes of each second */
static signed char sec_tg[MAXSEC];    /* the target of each second (-1 none) */
static UBYTE sec_first[MAXSEC];       /* the first second of a slice (connecting) */
static struct {
  UWORD sec;
  UBYTE t;
  UBYTE fail;
  ULONG ms;
} req[MAXREQ];
static int nreq;

/* --- the window ------------------------------------------------------------------ */

static struct Window *win;
static struct RastPort *rp;
static struct Screen *scr;
static LONG pen_grid = -1, pen_red = -1, pen_text = 1, pen_back = 0;
static int quit, duration = 180;
static ULONG t_start, last_draw, last_data;
static int cur_t = -1, cur_sec = -1, in_stall;
static char status[160];
static char *logbuf;
static ULONG loglen, logcap;

#define W 624
#define H 432
#define GX 44                         /* charts: left edge, width */
#define GW 580
#define G1Y 26                        /* speed chart */
#define G1H 150
#define G2Y 198                       /* response chart */
#define G2H 70
#define TY 290                        /* the table */

static void logf_(const char *fmt, ...)
{
  char line[300];
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
  va_end(ap);
  if (n < 0) n = 0;
  if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
  line[n++] = '\n';
  if (logbuf != NULL && loglen + n < logcap) {
    memcpy(logbuf + loglen, line, n);
    loglen += n;
  }
}

static void text(int x, int y, LONG pen, const char *fmt, ...)
{
  char s[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s, sizeof(s), fmt, ap);
  va_end(ap);
  SetAPen(rp, pen);
  SetBPen(rp, pen_back);
  SetDrMd(rp, JAM2);
  Move(rp, win->BorderLeft + x, win->BorderTop + y + rp->TxBaseline);
  Text(rp, (STRPTR)s, strlen(s));
}

static void box(int x, int y, int w, int h, LONG pen)
{
  if (w <= 0 || h <= 0) return;
  SetAPen(rp, pen);
  RectFill(rp, win->BorderLeft + x, win->BorderTop + y, win->BorderLeft + x + w - 1,
           win->BorderTop + y + h - 1);
}

static void line(int x0, int y0, int x1, int y1, LONG pen)
{
  SetAPen(rp, pen);
  Move(rp, win->BorderLeft + x0, win->BorderTop + y0);
  Draw(rp, win->BorderLeft + x1, win->BorderTop + y1);
}

/* the scale of the speed chart: KB/s at the top */
static ULONG top_kbs = 500;

static int sec_x(int s)
{
  return GX + s * GW / duration;
}

static void draw_speed_bar(int s)
{
  int x0 = sec_x(s), x1 = sec_x(s + 1), h;
  ULONG k = sec_bytes[s] / 1024UL;
  if (x1 <= x0) x1 = x0 + 1;
  box(x0, G1Y, x1 - x0, G1H, pen_back);
  if (sec_tg[s] >= 0) {
    h = (int)(k >= top_kbs ? G1H : k * G1H / top_kbs);
    if (h == 0 && sec_bytes[s] == 0) {
      box(x0, G1Y + G1H - 2, x1 - x0, 2, pen_red);      /* nothing in this second */
    } else {
      if (h < 1) h = 1;
      box(x0, G1Y + G1H - h, x1 - x0, h, tg[(int)sec_tg[s]].pen);
    }
  }
}

static ULONG top_ms = 2000;

static void draw_req(int i)
{
  int x = sec_x(req[i].sec) + 1, y;
  if (req[i].fail) {
    line(x - 2, G2Y + 1, x + 2, G2Y + 5, pen_red);
    line(x - 2, G2Y + 5, x + 2, G2Y + 1, pen_red);
    return;
  }
  y = G2Y + G2H - 2 - (int)(req[i].ms >= top_ms ? G2H - 4 : req[i].ms * (G2H - 4) / top_ms);
  box(x - 1, y - 1, 3, 3, tg[req[i].t].pen);
}

static void draw_frame(void)
{
  int i;
  box(0, 0, W, H, pen_back);
  text(4, 2, pen_text, T("Predkosc (KB/s), co sekunde; kolor = serwer, czerwone = brak danych",
                         "Speed (KB/s), every second; colour = server, red = no data"));
  line(GX - 1, G1Y, GX - 1, G1Y + G1H, pen_grid);
  line(GX - 1, G1Y + G1H, GX + GW, G1Y + G1H, pen_grid);
  for (i = 1; i <= 4; i++) {
    int y = G1Y + G1H - i * G1H / 4;
    line(GX, y, GX + GW, y, pen_grid);
    text(2, y - 3, pen_text, "%4lu", (unsigned long)(top_kbs * i / 4));
  }
  for (i = 0; i <= duration; i += 30) {
    int x = sec_x(i);
    line(x, G1Y + G1H, x, G1Y + G1H + 3, pen_grid);
    text(x - 8 < GX + GW - 24 ? x - 8 : GX + GW - 24, G1Y + G1H + 4, pen_text, "%d", i);
  }
  text(4, G2Y - 11, pen_text, T("Czas odpowiedzi (ms) kazdego zapytania; czerwony X = blad",
                                "Response time (ms) of every request; red X = error"));
  line(GX - 1, G2Y, GX - 1, G2Y + G2H, pen_grid);
  line(GX - 1, G2Y + G2H, GX + GW, G2Y + G2H, pen_grid);
  for (i = 1; i <= 2; i++) {
    int y = G2Y + G2H - i * G2H / 2;
    line(GX, y, GX + GW, y, pen_grid);
    text(2, y - 3, pen_text, "%4lu", (unsigned long)(top_ms * i / 2));
  }
  for (i = 0; i < duration && i < MAXSEC; i++) draw_speed_bar(i);
  for (i = 0; i < nreq; i++) draw_req(i);
}

static void draw_table(void)
{
  int i, y = TY;
  text(4, y, pen_text, T("Serwer        sr. KB/s  min   max  zaciecia bledy odpowiedz",
                         "Server        avg KB/s  min   max  stalls  errors response"));
  for (i = 0; i < NT; i++) {
    Target *t = &tg[i];
    y += 11;
    if (!t->on) {
      text(4, y, pen_text, "%-12s  %-46s", t->name,
           i == 3 ? T("(brak PRIVATE w WarpHub.prefs)", "(no PRIVATE in WarpHub.prefs)") : "-");
      continue;
    }
    box(4, y + 1, 8, 6, t->pen);
    text(16, y, pen_text, "%-10s %6lu %6lu %6lu %6d %6d %6lu ms   ", t->name,
         (unsigned long)(t->secs ? t->cbytes / 1024UL / t->secs : 0), (unsigned long)t->kmin,
         (unsigned long)t->kmax, t->stalls, t->errors,
         (unsigned long)(t->reqs ? t->ttfb_sum / t->reqs : 0));
  }
  text(4, TY + 11 * 6, pen_text, "%-76s", status);
}

static void handle_window(void)
{
  struct IntuiMessage *m;
  while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
    ULONG cl = m->Class;
    ReplyMsg((struct Message *)m);
    if (cl == IDCMP_CLOSEWINDOW) {
      quit = 1;
      hub_break = 1;
    } else if (cl == IDCMP_REFRESHWINDOW) {
      BeginRefresh(win);
      EndRefresh(win, TRUE);
      draw_frame();
      draw_table();
    }
  }
  if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
    quit = 1;
    hub_break = 1;
  }
}

/* --- time and the bins -------------------------------------------------------------- */

static int now_sec(void)
{
  return (int)((hub_ms() - t_start) / 1000UL);
}

/* a second ended: its bar, the min / max of its target */
static void close_sec(int s)
{
  Target *t;
  ULONG k;
  /* the bit of a second after the end of the test is not a second */
  if (s < 0 || s >= MAXSEC || s >= duration || sec_tg[s] < 0) return;
  t = &tg[(int)sec_tg[s]];
  k = sec_bytes[s] / 1024UL;
  if (!sec_first[s]) {
    /* the first second of a slice is connecting (TLS): not in avg / min / max */
    t->secs++;
    t->cbytes += sec_bytes[s];
    if (t->secs == 1 || k < t->kmin) t->kmin = k;
    if (k > t->kmax) t->kmax = k;
  }
  logf_("%4d %-10s %6lu KB/s%s", s, t->name, (unsigned long)k, sec_first[s] ? "  (start)" : "");
  if (k > top_kbs) {
    /* a faster line: a bigger scale, all drawn again */
    while (top_kbs < k) top_kbs = top_kbs < 1000 ? top_kbs * 2 : top_kbs + 1000;
    draw_frame();
  } else {
    draw_speed_bar(s);
  }
}

/* called often (data, waits): the seconds, stalls, the window */
static void tick(void)
{
  int s = now_sec();
  ULONG now = hub_ms();
  while (cur_sec < s && cur_sec < MAXSEC - 1) {
    if (cur_sec >= 0) close_sec(cur_sec);
    cur_sec++;
    if (cur_sec < MAXSEC) sec_tg[cur_sec] = (signed char)cur_t;
  }
  if (cur_t >= 0 && now - last_data > 1000UL && !in_stall) {
    in_stall = 1;
    tg[cur_t].stalls++;
  }
  if (now - last_draw >= 250UL) {
    last_draw = now;
    if (cur_sec >= 0 && cur_sec < MAXSEC && cur_sec < duration) draw_speed_bar(cur_sec);
    snprintf(status, sizeof(status), T("%s ... %d / %d s  (zamkniecie okna albo Ctrl-C konczy)",
                                       "%s ... %d / %d s  (close the window or Ctrl-C to stop)"),
             cur_t >= 0 ? tg[cur_t].name : "-", s, duration);
    draw_table();
  }
  handle_window();
}

static void wait_hook(void)
{
  tick();
}

/* --- the downloads ------------------------------------------------------------------ */

static ULONG slice_end, req_t0, req_first;

static int sink(void *user, const UBYTE *data, LONG len)
{
  ULONG now = hub_ms();
  int s;
  (void)user;
  (void)data;
  if (req_first == 0) req_first = now;
  tick();
  s = now_sec();
  if (s >= 0 && s < MAXSEC && cur_t >= 0) {
    sec_bytes[s] += len;
    sec_tg[s] = (signed char)cur_t;
  }
  tg[cur_t].bytes += len;
  last_data = now;
  in_stall = 0;
  return !quit && now < slice_end;     /* the slice is over: stop this download */
}

static void add_req(int t, int fail, ULONG ms)
{
  if (nreq >= MAXREQ) return;
  req[nreq].sec = (UWORD)(now_sec() < MAXSEC ? now_sec() : MAXSEC - 1);
  req[nreq].t = (UBYTE)t;
  req[nreq].fail = (UBYTE)fail;
  req[nreq].ms = ms;
  if (!fail && ms > top_ms) {
    while (top_ms < ms) top_ms *= 2;
    draw_frame();
  }
  draw_req(nreq);
  nreq++;
}

static void run_slice(int t, ULONG ms)
{
  Target *g = &tg[t];
  char path[300];
  char err[300];
  cur_t = t;
  slice_end = hub_ms() + ms;
  if (now_sec() >= 0 && now_sec() < MAXSEC) sec_first[now_sec()] = 1;
  last_data = hub_ms();
  in_stall = 0;
  /* the paths are plain ASCII; not hub_url_path: it would encode the "?" of
   * a query (Cloudflare: /__down?bytes=... -> 404) */
  snprintf(path, sizeof(path), "%s", g->path);
  while (!quit && hub_ms() < slice_end) {
    int status;
    if (g->c == NULL) {
      g->c = hub_conn_new(g->host, g->port);
      if (g->c == NULL) return;
      if (g->auth) {
        extern HubPrefs prefs;
        hub_conn_pin(g->c, prefs.priv_cert);
        if (prefs.priv_pass[0]) hub_conn_auth(g->c, prefs.priv_pass);
      }
    }
    req_t0 = hub_ms();
    req_first = 0;
    err[0] = 0;
    status = hub_get(g->c, path, sink, NULL, NULL, 0, err, sizeof(err));
    if (quit) break;
    if (req_first != 0) {
      g->reqs++;
      g->ttfb_sum += req_first - req_t0;
      add_req(t, 0, req_first - req_t0);
      logf_("     %-10s response %lu ms", g->name, (unsigned long)(req_first - req_t0));
    }
    if (status == 200 || (status == 401 && g->auth)) {
      if (status == 401 && req_first == 0) {
        /* no password: the response time of the (small) refusal */
        g->reqs++;
        g->ttfb_sum += hub_ms() - req_t0;
        add_req(t, 0, hub_ms() - req_t0);
        hub_conn_free(g->c);
        g->c = NULL;
        while (!quit && hub_ms() < slice_end && hub_ms() - req_t0 < 1000UL) tick();
      }
      continue;
    }
    if (status < 0 && hub_ms() >= slice_end && req_first != 0) {
      /* stopped by the end of the slice: not an error */
      hub_conn_free(g->c);
      g->c = NULL;
      break;
    }
    g->errors++;
    snprintf(g->last_err, sizeof(g->last_err), "%s", status < 0 ? err : "HTTP error");
    if (status > 0) snprintf(g->last_err, sizeof(g->last_err), "HTTP %d", status);
    add_req(t, 1, 0);
    logf_("     %-10s ERROR %s", g->name, g->last_err);
    hub_conn_free(g->c);
    g->c = NULL;
    /* a short pause, still drawing */
    {
      ULONG t0 = hub_ms();
      while (!quit && hub_ms() - t0 < 1000UL && hub_ms() < slice_end) tick();
    }
  }
}

/* --- the verdict ---------------------------------------------------------------- */

static void verdicts(void)
{
  int i, y = TY + 11 * 6;
  box(0, y, W, H - y, pen_back);
  text(4, y, pen_text, T("WYNIK:", "RESULT:"));
  logf_("");
  logf_(T("WYNIK", "RESULT"));
  for (i = 0; i < NT; i++) {
    Target *t = &tg[i];
    ULONG avg;
    const char *v;
    char s[200];
    if (!t->on) continue;
    avg = t->secs ? t->cbytes / 1024UL / t->secs : 0;
    if (t->reqs == 0) {
      v = T("BRAK POLACZENIA", "NO CONNECTION");
    } else if (t->stalls + t->errors >= 3 || (avg > 0 && t->kmin * 10 < avg && t->secs > 5)) {
      v = avg < 30 ? T("WOLNY I NIESTABILNY", "SLOW AND UNSTABLE") : T("NIESTABILNY", "UNSTABLE");
    } else if (avg < 30 && !(i == 3 && !t->bytes)) {
      v = T("WOLNY", "SLOW");
    } else {
      v = T("STABILNY", "STABLE");
    }
    if (i == 3 && t->bytes == 0 && t->reqs > 0) {
      snprintf(s, sizeof(s), T("%-10s %s (bez hasla - tylko odpowiedz %lu ms)",
                               "%-10s %s (no password - response only, %lu ms)"),
               t->name, t->stalls + t->errors ? v : T("ODPOWIADA", "ANSWERS"),
               (unsigned long)(t->ttfb_sum / t->reqs));
    } else {
      snprintf(s, sizeof(s), T("%-10s %s: sr. %lu KB/s, zaciec %d, bledow %d",
                               "%-10s %s: avg %lu KB/s, stalls %d, errors %d"),
               t->name, v, (unsigned long)avg, t->stalls, t->errors);
    }
    y += 11;
    box(4, y + 1, 8, 6, t->pen);
    text(16, y, pen_text, "%s", s);
    logf_("%s", s);
    if (t->last_err[0]) logf_("           %s", t->last_err);
  }
  {
    /* the line or one server: the test servers say it */
    int bad_all = 1, bad_some = 0;
    for (i = 0; i < 3; i++) {
      if (!tg[i].on) continue;
      if (tg[i].stalls + tg[i].errors < 3 && tg[i].reqs > 0) bad_all = 0; else bad_some = 1;
    }
    y += 13;
    if (bad_all) {
      text(4, y, pen_red, "%s", T("Wszystkie serwery sie zacinaja: problem jest w laczu / sieci tej Amigi.",
                                  "All servers stall: the problem is the line / network of this Amiga."));
      logf_("%s", T("Wszystkie serwery sie zacinaja: problem jest w laczu / sieci tej Amigi.",
                    "All servers stall: the problem is the line / network of this Amiga."));
    } else if (bad_some) {
      text(4, y, pen_text, "%s", T("Zacina sie tylko czesc serwerow: lacze dziala, problem po drodze do nich.",
                                   "Only some servers stall: the line works, the problem is on the way to them."));
      logf_("%s", T("Zacina sie tylko czesc serwerow: lacze dziala, problem po drodze do nich.",
                    "Only some servers stall: the line works, the problem is on the way to them."));
    } else {
      text(4, y, pen_text, "%s", T("Lacze stabilne.", "The line is stable."));
      logf_("%s", T("Lacze stabilne.", "The line is stable."));
    }
    text(4, y + 12, pen_text, T("Wyniki: " LOG_FILE ". Zamknij okno, by zakonczyc.",
                                "Results: " LOG_FILE ". Close the window to end."));
  }
}

/* --- main ------------------------------------------------------------------------- */

HubPrefs prefs;

static void read_duration(void)
{
  if (_WBenchMsg != NULL) {
    struct DiskObject *d;
    BPTR old = CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);
    d = GetDiskObject((STRPTR)_WBenchMsg->sm_ArgList[0].wa_Name);
    if (d != NULL) {
      STRPTR v = FindToolType((CONST_STRPTR *)d->do_ToolTypes, (CONST_STRPTR)"DURATION");
      if (v != NULL) duration = atoi((char *)v);
      FreeDiskObject(d);
    }
    CurrentDir(old);
  } else {
    LONG args[1] = {0};
    struct RDArgs *rd = ReadArgs((CONST_STRPTR)"DURATION/N", args, NULL);
    if (rd != NULL) {
      if (args[0]) duration = (int)*(LONG *)args[0];
      FreeArgs(rd);
    }
  }
  if (duration < 30) duration = 30;
  if (duration > MAXSEC - 30) duration = MAXSEC - 30;
}

int main(void)
{
  char err[300];
  ULONG sigs;
  int i, slice;
  (void)vers;

  read_duration();
  hub_prefs_load(&prefs, "PROGDIR:WarpHub.prefs");
  if (prefs.priv[0] == 0 && prefs.language[0] == 0) hub_prefs_load(&prefs, "SYS:WarpApps/WarpHub.prefs");
  pl = prefs.language[0] == 0 || strcmp(prefs.language, "pl") == 0;
  if (prefs.priv[0]) {
    const char *colon = strrchr(prefs.priv, ':');
    int n = colon != NULL ? (int)(colon - prefs.priv) : (int)strlen(prefs.priv);
    if (n > 0 && n < (int)sizeof(tg[3].host)) {
      memcpy(tg[3].host, prefs.priv, n);
      tg[3].host[n] = 0;
      if (colon != NULL) tg[3].port = atoi(colon + 1);
      tg[3].on = strlen(prefs.priv_cert) == 64;
    }
  }
  for (i = 0; i < MAXSEC; i++) sec_tg[i] = -1;
  logcap = 65536;
  logbuf = (char *)hub_malloc(logcap);

  scr = LockPubScreen(NULL);
  if (scr == NULL) return RETURN_FAIL;
  win = OpenWindowTags(NULL, WA_Left, 0, WA_Top, 12, WA_InnerWidth, W, WA_InnerHeight, H,
                       WA_Title, (ULONG)"WARP HUB - net monitor " NETMON_VERSION,
                       WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                       WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_PubScreen, (ULONG)scr,
                       WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW, TAG_END);
  if (win == NULL) {
    UnlockPubScreen(NULL, scr);
    return RETURN_FAIL;
  }
  rp = win->RPort;
  for (i = 0; i < NT; i++) {
    tg[i].pen = ObtainBestPen(scr->ViewPort.ColorMap, (ULONG)tg[i].rgb[0] * 0x01010101UL,
                              (ULONG)tg[i].rgb[1] * 0x01010101UL, (ULONG)tg[i].rgb[2] * 0x01010101UL,
                              OBP_Precision, PRECISION_GUI, TAG_END);
  }
  pen_red = ObtainBestPen(scr->ViewPort.ColorMap, 0xe0e0e0e0UL, 0x20202020UL, 0x20202020UL,
                          OBP_Precision, PRECISION_GUI, TAG_END);
  pen_grid = ObtainBestPen(scr->ViewPort.ColorMap, 0x80808080UL, 0x80808080UL, 0x80808080UL,
                           OBP_Precision, PRECISION_GUI, TAG_END);
  {
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    if (dri != NULL) {
      pen_text = dri->dri_Pens[TEXTPEN];
      pen_back = dri->dri_Pens[BACKGROUNDPEN];
      FreeScreenDrawInfo(scr, dri);
    }
  }
  draw_frame();
  snprintf(status, sizeof(status), "%s", T("Start sieci (AmiSSL, czas)...", "Starting the network (AmiSSL, time)..."));
  draw_table();

  logf_("WarpNetMonitor %s, %d s", NETMON_VERSION, duration);
  if (!hub_https_init(NULL, err, sizeof(err))) {
    snprintf(status, sizeof(status), "%.150s", err);
    draw_table();
    logf_("%s", err);
    goto wait_close;
  }
  logf_("AmiSSL: %s", hub_https_version());
  {
    char how[80];
    ULONG t = hub_net_time(NULL, how, sizeof(how), err, sizeof(err));
    if (t) hub_https_verify_time(t);
  }
  hub_wait_sigs = 1UL << win->UserPort->mp_SigBit;
  hub_wait_hook = wait_hook;

  t_start = hub_ms();
  last_draw = 0;
  for (slice = 0; !quit && hub_ms() - t_start < (ULONG)duration * 1000UL; slice++) {
    int t = slice % NT, k;
    for (k = 0; k < NT && !tg[t].on; k++) t = (t + 1) % NT;
    if (!tg[t].on) break;
    {
      ULONG left = (ULONG)duration * 1000UL - (hub_ms() - t_start);
      run_slice(t, left < SLICE_MS ? left : SLICE_MS);
    }
    /* the next target in the next slice: the loop index goes on */
    if (slice % NT != t) slice = t;
  }
  tick();
  if (cur_sec >= 0 && cur_sec < MAXSEC) close_sec(cur_sec);
  cur_t = -1;
  hub_wait_hook = NULL;
  hub_wait_sigs = 0;
  for (i = 0; i < NT; i++) {
    hub_conn_free(tg[i].c);
    tg[i].c = NULL;
  }
  draw_table();
  verdicts();
  if (logbuf != NULL) hub_write_file(LOG_FILE, logbuf, loglen);

wait_close:
  quit = 0;
  while (!quit) {
    sigs = Wait((1UL << win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
    if (sigs & SIGBREAKF_CTRL_C) break;
    handle_window();
  }
  hub_https_cleanup();
  memset(&prefs, 0, sizeof(prefs));
  for (i = 0; i < NT; i++) {
    if (tg[i].pen >= 0) ReleasePen(scr->ViewPort.ColorMap, tg[i].pen);
  }
  if (pen_red >= 0) ReleasePen(scr->ViewPort.ColorMap, pen_red);
  if (pen_grid >= 0) ReleasePen(scr->ViewPort.ColorMap, pen_grid);
  CloseWindow(win);
  UnlockPubScreen(NULL, scr);
  hub_free(logbuf);
  return RETURN_OK;
}
