/* hub_worker.c - the network process of WARP HUB (see hub_worker.h). */

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "hub_mem.h"
#include "hub_https.h"
#include "hub_time.h"
#include "hub_util.h"
#include "hub_worker.h"

#ifndef BUILD_UNIX
#define BUILD_UNIX 1795046400UL
#endif

static HubWork *work;
static struct Task *window_task;
static ULONG window_mask;
static struct Task *volatile net_task;
static BYTE job_signal = -1;
static volatile int alive;
static HubConn *conn;              /* kept for the host of the catalog */
static char conn_host[128];
static char err[256];
static ULONG last_signal_done;

static const char *const ntp_servers[] = {
  "pool.ntp.org", "time.cloudflare.com", "time.google.com", "europe.pool.ntp.org", NULL
};

static void tell_window(void)
{
  Signal(window_task, window_mask);
}

void hub_cache_image(const HubWork *w, const char *key, char *out, int outlen)
{
  snprintf(out, outlen, "%s/images/%s.png", w->cache, key);
}

void hub_cache_notes(const HubWork *w, const char *key, int lang, char *out, int outlen)
{
  static const char *const names[] = {"en", "pl", "de"};
  snprintf(out, outlen, "%s/notes/%s_%s.txt", w->cache, key, names[lang]);
}

void hub_cache_guide(const HubWork *w, const char *key, int n, char *out, int outlen)
{
  snprintf(out, outlen, "%s/guide/%s_%d.txt", w->cache, key, n);
}

/* --- getting files ------------------------------------------------------- */

typedef struct Mem {
  char *data;
  ULONG len, cap, max;
} Mem;

static int mem_sink(void *user, const UBYTE *data, LONG len)
{
  Mem *m = (Mem *)user;
  if (m->len + len + 1 > m->cap) {
    ULONG cap = m->cap ? m->cap * 2 : 16384;
    char *d;
    while (cap < m->len + len + 1) cap *= 2;
    if (m->len + len > m->max) return 0;
    d = (char *)hub_realloc(m->data, cap);
    if (d == NULL) return 0;
    m->data = d;
    m->cap = cap;
  }
  memcpy(m->data + m->len, data, len);
  m->len += len;
  m->data[m->len] = 0;
  return 1;
}

/* https://host/path -> memory (NUL-terminated); NULL with err */
static char *get_https(const char *url, ULONG max, ULONG *len)
{
  char host[128], path[600];
  const char *p = url + 8;
  int i, status;
  Mem m = {NULL, 0, 0, 0};
  m.max = max;
  for (i = 0; *p && *p != '/' && i < (int)sizeof(host) - 1; i++) host[i] = *p++;
  host[i] = 0;
  hub_url_path(path, sizeof(path), *p ? p : "/");
  if (conn == NULL || strcmp(conn_host, host) != 0) {
    hub_conn_free(conn);
    conn = hub_conn_new(host, 443);
    strcpy(conn_host, host);
    if (conn == NULL) {
      snprintf(err, sizeof(err), "No memory.");
      return NULL;
    }
  }
  status = hub_get(conn, path, mem_sink, &m, NULL, 0, err, sizeof(err));
  if (status != 200) {
    if (status > 0) snprintf(err, sizeof(err), "HTTP %d: %s", status, url);
    hub_free(m.data);
    return NULL;
  }
  if (m.data == NULL) mem_sink(&m, (const UBYTE *)"", 0);
  if (len != NULL) *len = m.len;
  return m.data;
}

/* a URL or a file name -> memory */
static char *get_any(const char *src, ULONG max, ULONG *len)
{
  char *data;
  if (strncmp(src, "https://", 8) == 0) return get_https(src, max, len);
  data = hub_read_file(src, max, len);
  if (data == NULL) snprintf(err, sizeof(err), "Cannot read %s.", src);
  return data;
}

static void sha_of(const void *data, ULONG len, char hex[65])
{
  HubSha *s = hub_sha_new();
  hex[0] = 0;
  if (s == NULL) return;
  hub_sha_add(s, (const UBYTE *)data, len);
  hub_sha_end(s, hex);
}

/* the cache file is there with this size and SHA-256 */
static int cached(const char *name, ULONG size, const char *sha)
{
  ULONG len;
  char hex[65];
  char *data = hub_read_file(name, size, &len);
  int ok;
  if (data == NULL) return 0;
  sha_of(data, len, hex);
  ok = len == size && strcmp(hex, sha) == 0;
  hub_free(data);
  return ok;
}

/* src -> cache file name, checked; 1 when it is there */
static int to_cache(const char *src, const char *name, ULONG size, const char *sha)
{
  ULONG len;
  char hex[65];
  char *data;
  if (cached(name, size, sha)) return 1;
  data = get_any(src, size, &len);
  if (data == NULL) return 0;
  sha_of(data, len, hex);
  if (len != size || strcmp(hex, sha) != 0) {
    snprintf(err, sizeof(err), "Wrong SHA-256: %s", src);
    hub_free(data);
    return 0;
  }
  len = hub_write_file(name, data, len);
  hub_free(data);
  return (int)len;
}

/* --- jobs ---------------------------------------------------------------- */

static void base_of(const char *src, char *out, int outlen)
{
  char *slash;
  strncpy(out, src, outlen - 1);
  out[outlen - 1] = 0;
  slash = strrchr(out, '/');
  if (slash == NULL) slash = strrchr(out, ':');
  if (slash != NULL) slash[1] = 0; else out[0] = 0;
}

static void job_refresh(HubWork *w)
{
  char *text, *copy, base[300], src[700], name[300];
  ULONG len;
  HubCatalog *cat;
  int i, lang, missing = 0;

  text = get_any(w->catalog_src, 4UL << 20, &len);
  if (text == NULL) {
    snprintf(w->msg, sizeof(w->msg), "%s", err);
    return;
  }
  copy = (char *)hub_malloc(len + 1);
  if (copy == NULL) {
    hub_free(text);
    snprintf(w->msg, sizeof(w->msg), "No memory.");
    return;
  }
  memcpy(copy, text, len + 1);
  cat = hub_catalog_parse(copy, err, sizeof(err));
  if (cat == NULL) {
    hub_free(text);
    snprintf(w->msg, sizeof(w->msg), "%s", err);
    return;
  }
  snprintf(name, sizeof(name), "%s/catalog.txt", w->cache);
  hub_write_file(name, text, len);
  hub_free(text);

  base_of(w->catalog_src, base, sizeof(base));
  for (i = 0; i < cat->ngames && !hub_break; i++) {
    HubGame *g = &cat->games[i];
    snprintf(w->file, sizeof(w->file), "%s", g->key);
    tell_window();
    if (g->image != NULL) {
      snprintf(src, sizeof(src), "%s%s", base, g->image);
      hub_cache_image(w, g->key, name, sizeof(name));
      if (!to_cache(src, name, g->image_size, g->image_sha)) missing++;
    }
    for (lang = 0; lang < HUB_LANGS; lang++) {
      HubFile *f = hub_game_file(g, g->notes[lang]);
      if (f == NULL) continue;
      if (g->local) {
        static const char *const names[] = {"en", "pl", "de"};
        /* test catalog: the repo is not public, the notes lie next to it */
        snprintf(src, sizeof(src), "%snotes/%s_%s.txt", base, g->key, names[lang]);
      } else {
        snprintf(src, sizeof(src), "https://raw.githubusercontent.com/%s/%s/releases/amiga/%s",
                 g->repo, g->branch, f->path);
      }
      hub_cache_notes(w, g->key, lang, name, sizeof(name));
      if (!to_cache(src, name, f->size, f->sha)) missing++;
    }
    {
      HubFile *gf[HUB_MAX_README];
      int k, ng = hub_guide_files(g, gf, HUB_MAX_README);
      for (k = 0; k < ng; k++) {
        if (g->local) {
          snprintf(src, sizeof(src), "%snotes/%s_guide%d.txt", base, g->key, k);
        } else {
          snprintf(src, sizeof(src), "https://raw.githubusercontent.com/%s/%s/releases/amiga/%s",
                   g->repo, g->branch, gf[k]->path);
        }
        hub_cache_guide(w, g->key, k, name, sizeof(name));
        if (!to_cache(src, name, gf[k]->size, gf[k]->sha)) missing++;
      }
    }
  }
  w->new_cat = cat;
  w->ok = 1;
  if (missing) {
    snprintf(w->msg, sizeof(w->msg), "%d pictures / histories missing: %s", missing, err);
  } else {
    w->msg[0] = 0;
  }
}

static void progress(void *user, const HubFile *f, ULONG done, ULONG total)
{
  HubWork *w = (HubWork *)user;
  w->done = done;
  w->total = total;
  if (strcmp(w->file, f->path) != 0) {
    snprintf(w->file, sizeof(w->file), "%s", f->path);
    last_signal_done = 0;
  }
  if (done - last_signal_done >= 32768 || done == total || last_signal_done == 0) {
    last_signal_done = done ? done : 1;
    tell_window();
  }
}

static void job_install(HubWork *w)
{
  HubGame *g = hub_catalog_find(w->cat, w->key);
  if (g == NULL) {
    snprintf(w->msg, sizeof(w->msg), "No game %s.", w->key);
    return;
  }
  hub_plan(g, w->dir, &w->plan);
  w->total = w->plan.bytes;
  w->done = 0;
  tell_window();
  if (w->plan.files == 0) {
    w->ok = 1;
    return;
  }
  w->ok = hub_install(g, w->dir, w->old_version, progress, w, w->msg, sizeof(w->msg));
}

/* --- the process ----------------------------------------------------------- */

static int net_open(HubWork *w)
{
  ULONG now, t = 0;
  int i;
  if (!hub_https_init(NULL, err, sizeof(err))) return 0;
  /* the time of a time server first (the clock of the Amiga is often wrong:
   * 1978 without a battery, once 2097), the clock only when none answers */
  now = hub_amiga_now();
  for (i = 0; t == 0 && ntp_servers[i] != NULL; i++) {
    t = hub_sntp(ntp_servers[i], err, sizeof(err));
  }
  if (t == 0) {
    if (HUB_CLOCK_OK(now)) return 1;
    snprintf(err, sizeof(err), "The clock of the Amiga is wrong and no time server "
             "answers: set the date in Prefs/Time.");
    return 0;
  }
  hub_https_verify_time(t);
  snprintf(w->net_info, sizeof(w->net_info), "NTP time for certificates");
  return 1;
}

static void net_main(void)
{
  HubWork *w = work;
  int net_ok;
  job_signal = AllocSignal(-1);
  net_ok = net_open(w);
  if (!net_ok) snprintf(w->net_info, sizeof(w->net_info), "%s", err);
  alive = 1;
  tell_window();
  while (job_signal >= 0) {
    Wait(1UL << job_signal);
    if (w->job == HUB_JOB_QUIT) break;
    if (w->job == HUB_JOB_NONE) continue;
    SetSignal(0, SIGBREAKF_CTRL_C);      /* a Stop of the last job */
    hub_break = 0;
    w->ok = 0;
    w->msg[0] = 0;
    w->file[0] = 0;
    w->done = w->total = 0;
    if (!net_ok) {
      snprintf(w->msg, sizeof(w->msg), "%s", w->net_info);
    } else if (w->job == HUB_JOB_REFRESH) {
      job_refresh(w);
    } else if (w->job == HUB_JOB_INSTALL) {
      job_install(w);
    }
    if (hub_break && !w->ok) snprintf(w->msg, sizeof(w->msg), "Stopped.");
    if (hub_conn_cipher(NULL)[0]) {
      snprintf(w->net_info, sizeof(w->net_info), "%s", hub_conn_cipher(NULL));
    }
    w->finished = w->job;
    w->job = HUB_JOB_NONE;
    w->busy = 0;
    tell_window();
  }
  hub_conn_free(conn);
  conn = NULL;
  hub_https_cleanup();
  if (job_signal >= 0) FreeSignal(job_signal);
  Forbid();
  alive = 0;
  net_task = NULL;
  tell_window();
}

int hub_worker_start(HubWork *w, int signal)
{
  work = w;
  window_task = FindTask(NULL);
  window_mask = 1UL << signal;
  alive = 0;
  net_task = (struct Task *)CreateNewProcTags(NP_Entry, (ULONG)net_main,
                                              NP_Name, (ULONG)"WARP HUB net",
                                              NP_StackSize, 65536,
                                              NP_Priority, -1,
                                              TAG_DONE);
  if (net_task == NULL) return 0;
  while (!alive && net_task != NULL) Wait(window_mask);
  return net_task != NULL;
}

void hub_worker_go(HubWork *w)
{
  if (net_task == NULL || w->busy) return;
  w->busy = 1;
  w->finished = 0;
  Signal((struct Task *)net_task, 1UL << job_signal);
}

void hub_worker_break(void)
{
  hub_break = 1;
  if (net_task != NULL) Signal((struct Task *)net_task, SIGBREAKF_CTRL_C);
}

void hub_worker_quit(HubWork *w)
{
  if (net_task == NULL) return;
  hub_worker_break();
  while (w->busy && net_task != NULL) Wait(window_mask);
  w->job = HUB_JOB_QUIT;
  Signal((struct Task *)net_task, 1UL << job_signal);
  while (net_task != NULL) Wait(window_mask);
}
