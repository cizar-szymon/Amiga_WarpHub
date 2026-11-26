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
static HubStamps sums;             /* cache files: size, date, SHA-256 (sums.txt) */
static int sums_loaded;
static char last_etag[100];        /* of the last get_https */

/* ETags of the catalogs (etags.txt: "<etag> <source>"): the next refresh
 * asks "If-None-Match" and gets 304 without the catalog when it is the same */
#define HUB_ETAGS 8
static struct {
  char etag[100];
  char src[200];
} etags[HUB_ETAGS];
static int netags, etags_loaded;

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

/* https://host/path -> memory (NUL-terminated); NULL with err. inm (not
 * NULL): asked "If-None-Match"; a 304 gives NULL and *not_modified = 1 */
static char *get_https_inm(const char *url, ULONG max, ULONG *len, const char *inm,
                           int *not_modified)
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
  if (not_modified != NULL) *not_modified = 0;
  hub_conn_if_none_match(conn, inm);
  status = hub_get(conn, path, mem_sink, &m, NULL, 0, err, sizeof(err));
  strcpy(last_etag, hub_conn_etag(conn));
  if (status == 304 && inm != NULL && not_modified != NULL) {
    *not_modified = 1;
    hub_free(m.data);
    return NULL;
  }
  if (status != 200) {
    if (status > 0) snprintf(err, sizeof(err), "HTTP %d: %s", status, url);
    hub_free(m.data);
    return NULL;
  }
  if (m.data == NULL) mem_sink(&m, (const UBYTE *)"", 0);
  if (len != NULL) *len = m.len;
  return m.data;
}

static char *get_https(const char *url, ULONG max, ULONG *len)
{
  return get_https_inm(url, max, len, NULL, NULL);
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
/* The size and the date of the file as remembered with this SHA-256
 * (sums.txt): not read again. Otherwise read, checked and remembered. */
static int cached(const char *name, ULONG size, const char *sha)
{
  ULONG len;
  char hex[65];
  char *data;
  int ok;
  const char *known = hub_stamps_get(&sums, name, &len);
  if (known != NULL && len == size && strcmp(known, sha) == 0) return 1;
  data = hub_read_file(name, size, &len);
  if (data == NULL) return 0;
  sha_of(data, len, hex);
  ok = len == size && strcmp(hex, sha) == 0;
  hub_free(data);
  if (ok) hub_stamps_set(&sums, name, sha);
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
  if (len) hub_stamps_set(&sums, name, sha);
  return (int)len;
}

/* --- ETags of the catalogs --------------------------------------------------- */

static void etags_load(HubWork *w)
{
  char name[300], *text, *line, *next;
  if (etags_loaded) return;
  etags_loaded = 1;
  snprintf(name, sizeof(name), "%s/etags.txt", w->cache);
  text = hub_read_file(name, 16384, NULL);
  for (line = text; line != NULL && *line && netags < HUB_ETAGS; line = next) {
    char *sp;
    next = strchr(line, '\n');
    if (next != NULL) *next++ = 0;
    sp = strchr(line, ' ');
    if (sp == NULL || sp - line >= (int)sizeof(etags[0].etag) ||
        strlen(sp + 1) >= sizeof(etags[0].src)) {
      continue;
    }
    memcpy(etags[netags].etag, line, sp - line);
    etags[netags].etag[sp - line] = 0;
    strcpy(etags[netags].src, sp + 1);
    netags++;
  }
  hub_free(text);
}

static const char *etag_of(const char *src)
{
  int i;
  for (i = 0; i < netags; i++) {
    if (strcmp(etags[i].src, src) == 0) return etags[i].etag;
  }
  return NULL;
}

/* remembers (etag "": forgets) the ETag of src, written at once */
static void etag_set(HubWork *w, const char *src, const char *etag)
{
  char name[300], buf[HUB_ETAGS * 310];
  int i, n = 0;
  if (strlen(src) >= sizeof(etags[0].src) || strlen(etag) >= sizeof(etags[0].etag)) return;
  for (i = 0; i < netags; i++) {
    if (strcmp(etags[i].src, src) == 0) break;
  }
  if (i == netags) {
    if (etag[0] == 0) return;
    if (netags == HUB_ETAGS) i = --netags;      /* the last one makes room */
    strcpy(etags[i].src, src);
    netags++;
  }
  if (strcmp(etags[i].etag, etag) == 0) return;
  strcpy(etags[i].etag, etag);
  for (i = 0; i < netags; i++) {
    if (etags[i].etag[0]) n += snprintf(buf + n, sizeof(buf) - n, "%s %s\n", etags[i].etag, etags[i].src);
  }
  snprintf(name, sizeof(name), "%s/etags.txt", w->cache);
  hub_write_file(name, buf, n);
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

/* --- the private source ---------------------------------------------------- */

static HubConn *priv_conn(HubWork *w, int with_password)
{
  HubConn *c = hub_conn_new(w->priv_host, w->priv_port);
  if (c == NULL) return NULL;
  hub_conn_pin(c, with_password ? w->priv_pin : "");
  if (with_password) hub_conn_auth(c, w->priv_pass);
  return c;
}

/* path of the private source -> memory; NULL with priv_status and err.
 * not_modified (not NULL): a 304 to the If-None-Match set on c gives NULL
 * and *not_modified = 1 (not an error) */
static char *get_private_inm(HubWork *w, HubConn *c, const char *path, ULONG max, ULONG *len,
                             int *not_modified)
{
  char p[600];
  int status;
  Mem m = {NULL, 0, 0, 0};
  m.max = max;
  hub_url_path(p, sizeof(p), path);
  status = hub_get(c, p, mem_sink, &m, NULL, 0, err, sizeof(err));
  if (status == 304 && not_modified != NULL) {
    *not_modified = 1;
    hub_free(m.data);
    return NULL;
  }
  if (status == 200) {
    if (m.data == NULL) mem_sink(&m, (const UBYTE *)"", 0);
    if (len != NULL) *len = m.len;
    return m.data;
  }
  hub_free(m.data);
  if (status == 401) {
    w->priv_status = HUB_PRIV_BAD_PASSWORD;
  } else if (status == 403) {
    w->priv_status = HUB_PRIV_BLOCKED;
  } else {
    w->priv_status = HUB_PRIV_ERROR;
    if (status > 0) snprintf(err, sizeof(err), "HTTP %d: %s", status, path);
  }
  snprintf(w->priv_msg, sizeof(w->priv_msg), "%s", err);
  return NULL;
}

static char *get_private(HubWork *w, HubConn *c, const char *path, ULONG max, ULONG *len)
{
  return get_private_inm(w, c, path, max, len, NULL);
}

/* the same as to_cache, from the private source (path on the server) */
static int priv_to_cache(HubWork *w, HubConn *c, const char *path, const char *name,
                         ULONG size, const char *sha)
{
  ULONG len;
  char hex[65];
  char *data;
  if (cached(name, size, sha)) return 1;
  if (c == NULL) return 0;
  data = get_private(w, c, path, size, &len);
  if (data == NULL) return 0;
  sha_of(data, len, hex);
  if (len != size || strcmp(hex, sha) != 0) {
    snprintf(err, sizeof(err), "Wrong SHA-256: %s", path);
    hub_free(data);
    return 0;
  }
  len = hub_write_file(name, data, len);
  hub_free(data);
  if (len) hub_stamps_set(&sums, name, sha);
  return (int)len;
}

/* first contact: the certificate of the server, without the password */
static void job_probe(HubWork *w)
{
  HubConn *c = priv_conn(w, 0);
  char *data;
  if (c == NULL) {
    snprintf(w->msg, sizeof(w->msg), "No memory.");
    return;
  }
  data = get_private(w, c, "/catalog.txt", 4UL << 20, NULL);
  hub_free(data);
  strcpy(w->priv_peer, hub_conn_peer(c));
  hub_conn_free(c);
  if (strlen(w->priv_peer) == 64) {
    w->priv_status = HUB_PRIV_PROBED;
    w->ok = 1;
  } else {
    snprintf(w->msg, sizeof(w->msg), "%s", w->priv_msg);
  }
}

static void job_refresh(HubWork *w)
{
  char *text, *copy, base[300], src[700], name[300], pname[300], *ptext = NULL;
  char pkey[200], pub_how[32], priv_how[32] = "-", pub_etag[100] = "", priv_etag[100] = "";
  ULONG len, plen = 0, t0 = hub_ms(), t1, t2;
  HubCatalog *cat;
  HubConn *pc = NULL;
  int i, lang, missing = 0, same = 0, psame = 0;

  if (!sums_loaded) {
    snprintf(name, sizeof(name), "%s/sums.txt", w->cache);
    hub_stamps_load(&sums, name);
    sums_loaded = 1;
  }
  etags_load(w);
  snprintf(name, sizeof(name), "%s/catalog.txt", w->cache);
  snprintf(pname, sizeof(pname), "%s/private.txt", w->cache);
  if (strncmp(w->catalog_src, "https://", 8) == 0) {
    /* 304: the catalog of the cache is the one of the server */
    const char *et = etag_of(w->catalog_src);
    int nm = 0;
    text = NULL;
    if (et != NULL) {
      text = get_https_inm(w->catalog_src, 4UL << 20, &len, et, &nm);
      if (nm) {
        text = hub_read_file(name, 4UL << 20, &len);
        same = text != NULL;
      }
    }
    /* no ETag yet, or 304 but the cache file is gone: the whole catalog */
    if (text == NULL && (et == NULL || nm)) text = get_https(w->catalog_src, 4UL << 20, &len);
    if (text != NULL && !same) strcpy(pub_etag, last_etag);
  } else {
    text = get_any(w->catalog_src, 4UL << 20, &len);
  }
  if (text == NULL) {
    snprintf(w->msg, sizeof(w->msg), "%s", err);
    return;
  }
  if (same) {
    strcpy(pub_how, "304");
  } else {
    snprintf(pub_how, sizeof(pub_how), "%lu B", (unsigned long)len);
  }
  t1 = hub_ms();
  if (w->priv_host[0] && strlen(w->priv_pin) == 64) {
    snprintf(pkey, sizeof(pkey), "private %s:%d", w->priv_host, w->priv_port);
    pc = priv_conn(w, 1);
    if (pc != NULL) {
      const char *et = etag_of(pkey);
      hub_conn_if_none_match(pc, et);
      ptext = get_private_inm(w, pc, "/catalog.txt", 4UL << 20, &plen, et != NULL ? &psame : NULL);
      if (psame) {
        ptext = hub_read_file(pname, 4UL << 20, &plen);
        if (ptext == NULL) {
          psame = 0;
          ptext = get_private(w, pc, "/catalog.txt", 4UL << 20, &plen);
        }
      }
      if (ptext != NULL && !psame) strcpy(priv_etag, hub_conn_etag(pc));
    }
    if (ptext != NULL) {
      w->priv_status = HUB_PRIV_OK;
      if (psame) {
        strcpy(priv_how, "304");
      } else {
        snprintf(priv_how, sizeof(priv_how), "%lu B", (unsigned long)plen);
      }
    }
  }
  t2 = hub_ms();
  /* the public catalog, then the private one: its games take the place of
   * the same public ones (hub_catalog_parse) */
  copy = (char *)hub_malloc(len + plen + 2);
  if (copy == NULL) {
    hub_free(text);
    hub_free(ptext);
    hub_conn_free(pc);
    snprintf(w->msg, sizeof(w->msg), "No memory.");
    return;
  }
  memcpy(copy, text, len + 1);
  if (ptext != NULL) {
    copy[len] = '\n';
    memcpy(copy + len + 1, ptext, plen + 1);
  }
  w->cat_sum = hub_fnv(copy, ptext != NULL ? len + 1 + plen : len);
  cat = hub_catalog_parse(copy, err, sizeof(err));
  if (cat == NULL) {
    hub_free(text);
    hub_free(ptext);
    hub_conn_free(pc);
    snprintf(w->msg, sizeof(w->msg), "%s", err);
    return;
  }
  /* the public catalog goes to catalog.txt (shown at the start); the private
   * one to private.txt, read only after a login answered 304 - without a
   * login (or the network) the private games are not shown. The ETag only
   * with the file it belongs to. */
  if (!same && hub_write_file(name, text, len) && strncmp(w->catalog_src, "https://", 8) == 0) {
    etag_set(w, w->catalog_src, pub_etag);
  }
  hub_free(text);
  if (ptext != NULL && !psame && hub_write_file(pname, ptext, plen)) etag_set(w, pkey, priv_etag);
  hub_free(ptext);

  base_of(w->catalog_src, base, sizeof(base));
  for (i = 0; i < cat->ngames && !hub_break; i++) {
    HubGame *g = &cat->games[i];
    snprintf(w->file, sizeof(w->file), "%s", g->key);
    w->chk_index = i + 1;
    w->chk_count = cat->ngames;
    w->loading_games = 1;
    tell_window();
    tell_window();
    if (g->priv) {
      /* from the private source: the same cache files, checked the same way */
      HubFile *gf[HUB_MAX_README];
      int k, ng;
      if (g->image != NULL) {
        snprintf(src, sizeof(src), "/%s", g->image);
        hub_cache_image(w, g->key, name, sizeof(name));
        if (!priv_to_cache(w, pc, src, name, g->image_size, g->image_sha)) missing++;
      }
      for (lang = 0; lang < HUB_LANGS; lang++) {
        HubFile *f = hub_game_file(g, g->notes[lang]);
        if (f == NULL) continue;
        snprintf(src, sizeof(src), "/files/%s/%s", g->key, f->path);
        hub_cache_notes(w, g->key, lang, name, sizeof(name));
        if (!priv_to_cache(w, pc, src, name, f->size, f->sha)) missing++;
      }
      ng = hub_guide_files(g, gf, HUB_MAX_README);
      for (k = 0; k < ng; k++) {
        snprintf(src, sizeof(src), "/files/%s/%s", g->key, gf[k]->path);
        hub_cache_guide(w, g->key, k, name, sizeof(name));
        if (!priv_to_cache(w, pc, src, name, gf[k]->size, gf[k]->sha)) missing++;
      }
      continue;
    }
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
  hub_conn_free(pc);
  snprintf(name, sizeof(name), "%s/sums.txt", w->cache);
  hub_stamps_save(&sums, name);
  snprintf(w->timing, sizeof(w->timing),
           "catalog %lu ms (%s), private %lu ms (%s), pictures and texts %lu ms (%d games)",
           (unsigned long)(t1 - t0), pub_how, (unsigned long)(t2 - t1), priv_how,
           (unsigned long)(hub_ms() - t2), cat->ngames);
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

static void check_progress(void *user, const HubFile *f, int index, int count, ULONG all_done, ULONG all_total)
{
  HubWork *w = (HubWork *)user;
  w->chk_index = index;
  w->chk_count = count;
  w->done = all_done;
  w->total = all_total;
  if (f != NULL) {
    snprintf(w->file, sizeof(w->file), "%s", f->path);
  }
  tell_window();
}

static void job_install(HubWork *w)
{
  HubGame *g = hub_catalog_find(w->cat, w->key);
  if (g == NULL) {
    snprintf(w->msg, sizeof(w->msg), "No game %s.", w->key);
    return;
  }
  {
    /* the manifest of the installed files: files with the remembered size
     * and date are not read again (a reinstall after one deleted file of
     * ZOD read all 490 MB before 0.17) */
    char mname[128];
    HubStamps man;
    ULONG t0 = hub_ms(), t1;
    int had, trust;
    memset(&man, 0, sizeof(man));
    snprintf(mname, sizeof(mname), "%s/files-%s.txt", w->cache, g->key);
    hub_stamps_load(&man, mname);
    had = man.n > 0;
    w->checking = 1;
    w->chk_index = 0;
    w->chk_count = 0;
    /* the manifest; without it a repair of the same version trusts the sizes
     * (user's decision 26.11.2026); an update without it, an unknown version or
     * FULLCHECK: every file is read */
    trust = w->full_check ? HUB_TRUST_NONE : had ? HUB_TRUST_MANIFEST :
            (w->old_version[0] && strcmp(w->old_version, g->version) == 0) ? HUB_TRUST_SIZE : HUB_TRUST_NONE;
    hub_plan_manifest(g, w->dir, &w->plan, check_progress, w, &man, trust);
    w->checking = 0;
    t1 = hub_ms();
    if (w->plan.files < 0) {
      snprintf(w->msg, sizeof(w->msg), "Stopped.");
      w->plan.files = 0;
      w->ok = 0;
    } else {
      w->total = w->plan.bytes;
      w->done = 0;
      tell_window();
      if (w->plan.files == 0) {
        w->ok = 1;
      } else {
        w->ok = hub_install(g, w->dir, w->old_version, progress, w, w->msg, sizeof(w->msg));
        if (w->ok) hub_manifest_installed(g, w->dir, &man);
      }
    }
    hub_stamps_save(&man, mname);
    hub_stamps_free(&man);
    snprintf(w->timing, sizeof(w->timing), "%s check %lu ms (%d files read, %d from the manifest%s), "
             "download %lu ms (%d files, %lu B)", g->key, (unsigned long)(t1 - t0), w->plan.read,
             w->plan.trusted, trust == HUB_TRUST_MANIFEST ? "" : trust == HUB_TRUST_SIZE ?
             ", no manifest: sizes (same version)" : w->full_check ? ", FULLCHECK" : ", no manifest: full check",
             (unsigned long)(hub_ms() - t1), w->plan.files, (unsigned long)w->plan.bytes);
  }
}

/* the window "Clock": the time from the internet (the user pressed the
 * button; the window sets the clock with its zone) */
extern struct Library *SocketBase;     /* hub_https.c */

static void job_time(HubWork *w)
{
  char how[80] = "";
  w->time_utc = 0;
  w->time_how[0] = 0;
  if (SocketBase == NULL) {
    snprintf(w->msg, sizeof(w->msg), "No TCP/IP stack (bsdsocket.library).");
    return;
  }
  w->time_utc = hub_net_time(NULL, how, sizeof(how), w->msg, sizeof(w->msg));
  w->time_ms = hub_ms();
  if (w->time_utc) {
    strcpy(w->time_how, how);
    w->msg[0] = 0;
    w->ok = 1;
  }
}

/* --- the process ----------------------------------------------------------- */

static int net_open(HubWork *w)
{
  ULONG now, t;
  char how[80] = "";
  if (!hub_https_init(NULL, err, sizeof(err))) return 0;
  /* the time of a time server first (the clock of the Amiga is often wrong:
   * 1978 without a battery, once 2097), then the Date of a web server (port
   * 80, when UDP 123 is blocked), the clock only when none answers */
  now = hub_amiga_now();
  t = hub_net_time(NULL, how, sizeof(how), err, sizeof(err));
  if (t == 0) {
    if (HUB_CLOCK_OK(now)) return 1;
    snprintf(err, sizeof(err), "The clock of the Amiga is wrong and no time server answers "
             "(NTP, UDP 123; HTTP, port 80): set the date in Prefs/Time.");
    return 0;
  }
  hub_https_verify_time(t);
  snprintf(w->net_info, sizeof(w->net_info), "Time for certificates: %s", how);
  return 1;
}

static void net_main(void)
{
  HubWork *w = work;
  int net_ok;
  job_signal = AllocSignal(-1);
  w->net_ms = hub_ms();
  net_ok = net_open(w);
  w->net_ms = hub_ms() - w->net_ms;
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
    w->priv_status = HUB_PRIV_NONE;
    w->priv_msg[0] = 0;
    hub_install_private(w->priv_host, w->priv_port, w->priv_pin, w->priv_pass);
    if (w->job == HUB_JOB_TIME) {
      job_time(w);                       /* also without AmiSSL: only the network */
    } else if (!net_ok) {
      snprintf(w->msg, sizeof(w->msg), "%s", w->net_info);
    } else if (w->job == HUB_JOB_REFRESH) {
      job_refresh(w);
    } else if (w->job == HUB_JOB_INSTALL) {
      job_install(w);
    } else if (w->job == HUB_JOB_PROBE) {
      job_probe(w);
    }
    if (hub_break && !w->ok) snprintf(w->msg, sizeof(w->msg), "Stopped.");
    if (hub_conn_cipher(NULL)[0]) {
      snprintf(w->net_info, sizeof(w->net_info), "%s", hub_conn_cipher(NULL));
    }
    w->loading_games = 0;
    w->checking = 0;
    w->finished = w->job;
    w->job = HUB_JOB_NONE;
    w->busy = 0;
    tell_window();
  }
  hub_conn_free(conn);
  conn = NULL;
  hub_stamps_free(&sums);
  sums_loaded = 0;
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
