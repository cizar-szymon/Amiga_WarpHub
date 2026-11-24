/* hub_install.c - install / update a game (see hub_install.h).
 *
 * Every file checked with the SHA-256 of the catalog, the files from
 * https://raw.githubusercontent.com/<repo>/<branch>/releases/amiga/<path>. */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hub_mem.h"
#include "hub_install.h"

#define RAW_HOST "raw.githubusercontent.com"
#define NAME_LEN 512
#define BUF_SIZE 32768

static UBYTE *buf;
static char priv_host[128], priv_pin[65], priv_pass[64];
static int priv_port;

void hub_install_private(const char *host, int port, const char *pin, const char *password)
{
  strncpy(priv_host, host, sizeof(priv_host) - 1);
  priv_port = port;
  strncpy(priv_pin, pin, sizeof(priv_pin) - 1);
  strncpy(priv_pass, password, sizeof(priv_pass) - 1);
}

static int full_name(char *out, int outlen, const char *dest, const char *path,
                     const char *suffix)
{
  int n;
  strncpy(out, dest, outlen - 1);
  out[outlen - 1] = 0;
  if (!AddPart((STRPTR)out, (CONST_STRPTR)path, outlen)) return 0;
  n = (int)strlen(out);
  if (n + (int)strlen(suffix) >= outlen) return 0;
  strcpy(out + n, suffix);
  return 1;
}

const char *hub_local_path(const HubGame *g, const char *path)
{
  const char *slash = strchr(path, '/');
  return g->flat && slash != NULL ? slash + 1 : path;
}

void hub_game_dir(HubGame *g, char *out, int outlen)
{
  if (g->flat) {               /* straight into the chosen drawer */
    out[0] = 0;
    return;
  }
  const char *p = g->files[0].path;
  int i;
  for (i = 0; i < g->nfiles; i++) {
    if (strchr(g->files[i].path, '/') != NULL) {
      p = g->files[i].path;
      break;
    }
  }
  for (i = 0; p[i] && p[i] != '/' && i < outlen - 1; i++) out[i] = p[i];
  out[i] = 0;
  if (p[i] != '/' && i > 5 && strcmp(out + i - 5, ".info") == 0) out[i - 5] = 0;
}

/* size and SHA-256 of a local file; 0 when there is none */
static int local_file(const char *name, ULONG want_size, ULONG *size, char sha[65])
{
  BPTR fh;
  LONG n;
  HubSha *s;
  fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
  if (fh == 0) return 0;
  *size = 0;
  sha[0] = 0;
  if (Seek(fh, 0, OFFSET_END) >= 0) {
    LONG end = Seek(fh, 0, OFFSET_BEGINNING);
    *size = (ULONG)end;
    if (*size != want_size) {      /* differs anyway: no need to read it */
      Close(fh);
      return 1;
    }
  }
  s = hub_sha_new();
  if (s == NULL) {
    Close(fh);
    return 1;
  }
  *size = 0;
  while ((n = Read(fh, buf, BUF_SIZE)) > 0) {
    hub_sha_add(s, buf, n);
    *size += n;
  }
  Close(fh);
  hub_sha_end(s, sha);
  return 1;
}

static int alloc_buf(void)
{
  if (buf == NULL) buf = (UBYTE *)hub_malloc(BUF_SIZE);
  return buf != NULL;
}

void hub_plan(HubGame *g, const char *dest, HubPlan *plan)
{
  char name[NAME_LEN], sha[65];
  int i;
  memset(plan, 0, sizeof(*plan));
  if (!alloc_buf()) return;
  for (i = 0; i < g->nfiles; i++) {
    HubFile *f = &g->files[i];
    ULONG size = 0;
    int here;
    if (f->flag == 'X' ||
        !full_name(name, sizeof(name), dest, hub_local_path(g, f->path), "")) {
      f->need = 0;
      continue;
    }
    here = local_file(name, f->size, &size, sha);
    if (f->flag == 'K') {
      f->need = !here;
      if (here) plan->kept++;
    } else {
      f->need = !here || size != f->size || strcmp(sha, f->sha) != 0;
    }
    if (f->need) {
      plan->files++;
      plan->bytes += f->size;
    }
  }
}

static void make_dirs(const char *dest, const char *path)
{
  char part[NAME_LEN], name[NAME_LEN];
  int i;
  for (i = 0; path[i] != 0 && i < NAME_LEN - 1; i++) {
    if (path[i] == '/') {
      BPTR lock;
      memcpy(part, path, i);
      part[i] = 0;
      if (!full_name(name, sizeof(name), dest, part, "")) return;
      lock = Lock((CONST_STRPTR)name, ACCESS_READ);
      if (lock == 0) lock = CreateDir((CONST_STRPTR)name);
      if (lock != 0) UnLock(lock);
    }
  }
}

typedef struct Fetch {
  BPTR fh;
  HubSha *sha;
  ULONG got, limit;
  ULONG *done;
  ULONG total;
  const HubFile *file;
  HubProgress progress;
  void *user;
  int write_failed;
} Fetch;

static int fetch_sink(void *user, const UBYTE *data, LONG len)
{
  Fetch *f = (Fetch *)user;
  if (f->got + (ULONG)len > f->limit) return 0;
  if (Write(f->fh, (APTR)data, len) != len) {
    f->write_failed = 1;
    return 0;
  }
  hub_sha_add(f->sha, data, len);
  f->got += len;
  *f->done += len;
  if (f->progress != NULL) f->progress(f->user, f->file, *f->done, f->total);
  return 1;
}

static int fetch_one(HubConn *c, HubGame *g, const char *dest, HubFile *file,
                     ULONG *done, ULONG total, HubProgress progress, void *user,
                     char *err, int errlen)
{
  char name[NAME_LEN], url[NAME_LEN * 2], raw[NAME_LEN * 2], sha[65];
  Fetch f;
  int status;

  if (g->priv) {
    snprintf(raw, sizeof(raw), "/files/%s/%s", g->key, file->path);
  } else {
    snprintf(raw, sizeof(raw), "/%s/%s/releases/amiga/%s", g->repo, g->branch, file->path);
  }
  hub_url_path(url, sizeof(url), raw);
  make_dirs(dest, hub_local_path(g, file->path));
  if (!full_name(name, sizeof(name), dest, hub_local_path(g, file->path), ".upd")) {
    snprintf(err, errlen, "Path too long: %s", file->path);
    return 0;
  }
  memset(&f, 0, sizeof(f));
  f.fh = Open((CONST_STRPTR)name, MODE_NEWFILE);
  if (f.fh == 0) {
    snprintf(err, errlen, "Cannot write %s (error %ld).", name, (long)IoErr());
    return 0;
  }
  f.sha = hub_sha_new();
  f.limit = file->size;
  f.done = done;
  f.total = total;
  f.file = file;
  f.progress = progress;
  f.user = user;
  if (f.sha == NULL) {
    Close(f.fh);
    DeleteFile((CONST_STRPTR)name);
    snprintf(err, errlen, "No memory.");
    return 0;
  }
  status = hub_get(c, url, fetch_sink, &f, NULL, 0, err, errlen);
  Close(f.fh);
  hub_sha_end(f.sha, sha);
  if (status == 200 && f.got == file->size && strcmp(sha, file->sha) == 0) return 1;
  DeleteFile((CONST_STRPTR)name);
  if (f.write_failed) {
    snprintf(err, errlen, "Cannot write %s (disk full?).", file->path);
  } else if (status > 0 && status != 200) {
    snprintf(err, errlen, "%s: HTTP %d.", file->path, status);
  } else if (status == 200) {
    snprintf(err, errlen, "%s: wrong %s - not taken.", file->path,
             f.got != file->size ? "size" : "SHA-256");
  }
  return 0;
}

/* a program: a file of the game drawer whose icon is a player's file */
static int is_program(HubGame *g, const HubFile *f)
{
  const char *slash = strchr(f->path, '/');
  int i, n = (int)strlen(f->path);
  if (slash == NULL || strchr(slash + 1, '/') != NULL) return 0;
  for (i = 0; i < g->nfiles; i++) {
    const char *p = g->files[i].path;
    if (g->files[i].flag == 'K' && (int)strlen(p) == n + 5 &&
        strncmp(p, f->path, n) == 0 && strcmp(p + n, ".info") == 0) {
      return 1;
    }
  }
  return 0;
}

static void remove_partial(HubGame *g, const char *dest)
{
  char name[NAME_LEN];
  int i;
  for (i = 0; i < g->nfiles; i++) {
    if (g->files[i].need &&
        full_name(name, sizeof(name), dest, hub_local_path(g, g->files[i].path), ".upd")) {
      DeleteFile((CONST_STRPTR)name);
    }
  }
}

static int put_in_place(HubGame *g, const char *dest, const char *old_version,
                        char *err, int errlen)
{
  char from[NAME_LEN], to[NAME_LEN], old[NAME_LEN], dir[64], rel[NAME_LEN];
  int i, ok = 1;
  hub_game_dir(g, dir, sizeof(dir));
  for (i = 0; i < g->nfiles; i++) {
    HubFile *f = &g->files[i];
    BPTR lock;
    if (!f->need) continue;
    full_name(from, sizeof(from), dest, hub_local_path(g, f->path), ".upd");
    full_name(to, sizeof(to), dest, hub_local_path(g, f->path), "");
    lock = Lock((CONST_STRPTR)to, ACCESS_READ);
    if (lock != 0) {
      UnLock(lock);
      if (is_program(g, f) && g->flat) {
        /* a program straight in the drawer (SYS:WarpApps/WarpHub): the old
         * one stays next to it as <name>.old (the drawer is not its own) */
        snprintf(old, sizeof(old), "%s.old", to);
        DeleteFile((CONST_STRPTR)old);
        if (!Rename((CONST_STRPTR)to, (CONST_STRPTR)old)) DeleteFile((CONST_STRPTR)to);
      } else if (is_program(g, f)) {
        /* the old program to <Game>/old/<name>-<version> */
        snprintf(rel, sizeof(rel), "%s/old/", dir);
        make_dirs(dest, rel);
        snprintf(rel, sizeof(rel), "%s/old/%s-%s", dir, strchr(f->path, '/') + 1,
                 old_version != NULL && old_version[0] ? old_version : "previous");
        full_name(old, sizeof(old), dest, rel, "");
        DeleteFile((CONST_STRPTR)old);
        if (!Rename((CONST_STRPTR)to, (CONST_STRPTR)old)) DeleteFile((CONST_STRPTR)to);
      } else {
        DeleteFile((CONST_STRPTR)to);
      }
    }
    if (!Rename((CONST_STRPTR)from, (CONST_STRPTR)to)) {
      if (ok) snprintf(err, errlen, "Cannot put %s in place (error %ld).", f->path,
                       (long)IoErr());
      ok = 0;
    }
  }
  return ok;
}

int hub_install(HubGame *g, const char *dest, const char *old_version,
                HubProgress progress, void *user, char *err, int errlen)
{
  HubConn *c;
  ULONG done = 0, total = 0;
  int i;
  BPTR lock;

  lock = Lock((CONST_STRPTR)dest, ACCESS_READ);
  if (lock == 0) {
    snprintf(err, errlen, "Drawer %s not found.", dest);
    return 0;
  }
  UnLock(lock);
  if (!alloc_buf()) {
    snprintf(err, errlen, "No memory.");
    return 0;
  }
  for (i = 0; i < g->nfiles; i++) {
    if (g->files[i].need) total += g->files[i].size;
  }
  if (g->priv && (priv_host[0] == 0 || strlen(priv_pin) != 64)) {
    snprintf(err, errlen, "Log in to the private source first.");
    return 0;
  }
  c = g->priv ? hub_conn_new(priv_host, priv_port) : hub_conn_new(RAW_HOST, 443);
  if (c == NULL) {
    snprintf(err, errlen, "No memory.");
    return 0;
  }
  if (g->priv) {
    hub_conn_pin(c, priv_pin);
    hub_conn_auth(c, priv_pass);
  }
  for (i = 0; i < g->nfiles; i++) {
    if (!g->files[i].need) continue;
    if (hub_break || !fetch_one(c, g, dest, &g->files[i], &done, total, progress,
                                user, err, errlen)) {
      if (hub_break) snprintf(err, errlen, "Stopped (Ctrl-C).");
      hub_conn_free(c);
      remove_partial(g, dest);
      return 0;
    }
  }
  hub_conn_free(c);
  return put_in_place(g, dest, old_version, err, errlen);
}

/* the part of a path before its last "/" ("a/b/c.txt" -> "a/b"); 0 when none */
static int parent_of(char *path)
{
  char *slash = strrchr(path, '/');
  if (slash == NULL) return 0;
  *slash = 0;
  return 1;
}

int hub_uninstall(HubGame *g, const char *dest, int *removed, int *left,
                  char *err, int errlen)
{
  char name[NAME_LEN], rel[NAME_LEN], dir[NAME_LEN];
  int i, pass;
  BPTR lock;
  *removed = 0;
  *left = 0;
  lock = Lock((CONST_STRPTR)dest, ACCESS_READ);
  if (lock == 0) {
    snprintf(err, errlen, "Drawer %s not found.", dest);
    return 0;
  }
  UnLock(lock);
  /* only the files of the catalog, each path checked: nothing else is touched */
  for (i = 0; i < g->nfiles; i++) {
    const char *p = hub_local_path(g, g->files[i].path);
    if (g->files[i].flag == 'X' || !hub_path_ok(p) ||
        !full_name(name, sizeof(name), dest, p, "")) continue;
    SetProtection((CONST_STRPTR)name, 0);
    if (DeleteFile((CONST_STRPTR)name)) {
      (*removed)++;
    } else if (IoErr() != ERROR_OBJECT_NOT_FOUND) {
      snprintf(err, errlen, "Cannot delete %s (error %ld).", p, (long)IoErr());
      return 0;
    }
  }
  /* the drawers of those files, deepest first; a drawer that still holds
   * something (the player's files) cannot be deleted and stays */
  for (pass = 0; pass < 4; pass++) {
    for (i = 0; i < g->nfiles; i++) {
      strncpy(rel, hub_local_path(g, g->files[i].path), sizeof(rel) - 1);
      rel[sizeof(rel) - 1] = 0;
      while (parent_of(rel) && rel[0] != 0) {
        if (full_name(name, sizeof(name), dest, rel, "")) DeleteFile((CONST_STRPTR)name);
      }
    }
  }
  hub_game_dir(g, dir, sizeof(dir));
  if (dir[0] != 0 && full_name(name, sizeof(name), dest, dir, "")) {
    lock = Lock((CONST_STRPTR)name, ACCESS_READ);
    if (lock != 0) {
      UnLock(lock);
      *left = 1;
    }
  }
  return 1;
}
