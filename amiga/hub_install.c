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
typedef struct CheckCtx {
  HubCheckProgress cb;
  void *user;
  const HubFile *f;
  int index, count;
  ULONG base, all_total;      /* bytes of the files before this one, of all files */
} CheckCtx;

static int local_file(const char *name, ULONG want_size, ULONG *size, char sha[65], CheckCtx *ctx)
{
  ULONG last_report = 0;
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
    if (ctx != NULL && ctx->cb != NULL && *size - last_report >= 262144UL) {
      last_report = *size;
      ctx->cb(ctx->user, ctx->f, ctx->index, ctx->count, ctx->base + *size, ctx->all_total);
    }
    if (hub_break) break;
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

void hub_plan_progress(HubGame *g, const char *dest, HubPlan *plan, HubCheckProgress cb, void *user)
{
  char name[NAME_LEN], sha[65];
  int i, count = 0;
  ULONG all_total = 0, base = 0;
  CheckCtx ctx;
  memset(plan, 0, sizeof(*plan));
  if (!alloc_buf()) return;
  for (i = 0; i < g->nfiles; i++) {
    if (g->files[i].flag != 'X') {
      count++;
      all_total += g->files[i].size;
    }
  }
  ctx.cb = cb;
  ctx.user = user;
  ctx.count = count;
  ctx.all_total = all_total;
  count = 0;
  for (i = 0; i < g->nfiles; i++) {
    HubFile *f = &g->files[i];
    ULONG size = 0;
    int here;
    if (f->flag == 'X' ||
        !full_name(name, sizeof(name), dest, hub_local_path(g, f->path), "")) {
      f->need = 0;
      continue;
    }
    if (hub_break) {
      plan->files = -1;
      return;
    }
    ctx.f = f;
    ctx.index = ++count;
    ctx.base = base;
    if (cb != NULL) cb(user, f, ctx.index, ctx.count, base, all_total);
    here = local_file(name, f->size, &size, sha, &ctx);
    base += f->size;
    if (hub_break) {
      plan->files = -1;
      return;
    }
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
  if (cb != NULL) cb(user, NULL, count, ctx.count, all_total, all_total);
}

void hub_plan(HubGame *g, const char *dest, HubPlan *plan)
{
  hub_plan_progress(g, dest, plan, NULL, NULL);
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
  ((HubFile *)f->file)->got = f->got;
  *f->done += len;
  if (f->progress != NULL) f->progress(f->user, f->file, *f->done, f->total);
  return 1;
}

/* the progress of a download while an earlier .upd is checked (a resumed
 * install: diabdat.mpq, 517 MB, takes minutes - the bar must move) */
typedef struct ResumeCtx {
  HubProgress progress;
  void *user;
  ULONG base;
} ResumeCtx;

static void resume_cb(void *user, const HubFile *f, int index, int count, ULONG all_done,
                      ULONG all_total)
{
  ResumeCtx *r = (ResumeCtx *)user;
  (void)index;
  (void)count;
  ((HubFile *)f)->got = all_done - r->base;
  if (r->progress != NULL) r->progress(r->user, f, all_done, all_total);
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
  {
    /* an earlier try that stopped (network, Stop) left this file whole and
     * right: taken as it is - an install of 80 MB goes on where it stopped */
    ULONG have = 0;
    char hs[65];
    ResumeCtx rc;
    CheckCtx cc;
    memset(&cc, 0, sizeof(cc));
    rc.progress = progress;
    rc.user = user;
    rc.base = *done;
    cc.cb = resume_cb;
    cc.user = &rc;
    cc.f = file;
    cc.base = *done;
    cc.all_total = total;
    file->state = HUB_F_CHECK;
    file->got = 0;
    if (local_file(name, file->size, &have, hs, &cc) && have == file->size &&
        strcmp(hs, file->sha) == 0) {
      file->got = file->size;
      file->state = HUB_F_DONE;
      *done += file->size;
      if (progress != NULL) progress(user, file, *done, total);
      return 1;
    }
  }
  file->state = HUB_F_GET;
  file->got = 0;
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
  if (status == 200 && f.got == file->size && strcmp(sha, file->sha) == 0) {
    file->state = HUB_F_DONE;
    return 1;
  }
  file->state = HUB_F_FAIL;
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
    g->files[i].state = g->files[i].need ? HUB_F_WAIT : HUB_F_NONE;
    g->files[i].got = 0;
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
    ULONG before = done;
    int ok = 0, attempt;
    if (!g->files[i].need) continue;
    /* up to 3 tries of a file the network lost (no data within the time
     * limit, connection lost), 2 s apart */
    for (attempt = 0; attempt < 3 && !hub_break; attempt++) {
      if (attempt > 0) {
        if (!hub_conn_net_error(c)) break;
        done = before;
        Delay(100);
      }
      ok = fetch_one(c, g, dest, &g->files[i], &done, total, progress, user, err, errlen);
      if (ok) break;
    }
    if (!ok) {
      /* the files that came whole stay (.upd, checked): the next Install /
       * Update takes them and downloads only the rest */
      if (hub_break) snprintf(err, errlen, "Stopped (Ctrl-C).");
      hub_conn_free(c);
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
    {
      /* a stopped install left it (kept for the next try) */
      char upd[NAME_LEN];
      if (full_name(upd, sizeof(upd), dest, p, ".upd")) DeleteFile((CONST_STRPTR)upd);
    }
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

/* --- the old versions and the bug reports of a game ------------------------------------------------------------- */

/* "bug" and at least three digits, then the end, a dot or a dash: bug007, bug007.txt, bug007-screen.bmp (not bugemail.txt) */
static int bug_name(const char *n)
{
  int digits = 0;
  if (strncmp(n, "bug", 3) != 0) return 0;
  n += 3;
  while (*n >= '0' && *n <= '9') {
    n++;
    digits++;
  }
  return digits >= 3 && (*n == 0 || *n == '.' || *n == '-');
}

typedef struct DirEntry {
  char name[64];
  int is_dir;
  ULONG size;
} DirEntry;

/* The entries of a drawer into e[] (at most max); returns how many. */
static int list_dir(const char *path, DirEntry *e, int max)
{
  struct FileInfoBlock *fib;
  BPTR lock;
  int n = 0;
  lock = Lock((CONST_STRPTR)path, ACCESS_READ);
  if (lock == 0) return 0;
  fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
  if (fib != NULL && Examine(lock, fib)) {
    while (n < max && ExNext(lock, fib)) {
      strncpy(e[n].name, (const char *)fib->fib_FileName, sizeof(e[n].name) - 1);
      e[n].name[sizeof(e[n].name) - 1] = 0;
      e[n].is_dir = fib->fib_DirEntryType > 0;
      e[n].size = (ULONG)fib->fib_Size;
      n++;
    }
  }
  if (fib != NULL) FreeDosObject(DOS_FIB, fib);
  UnLock(lock);
  return n;
}

/* Files and bytes of everything below a drawer. */
static void count_tree(const char *path, int depth, int *files, ULONG *bytes)
{
  DirEntry e[60];
  char child[NAME_LEN];
  int n, i;
  if (depth > 3) return;
  n = list_dir(path, e, 60);
  for (i = 0; i < n; i++) {
    if (e[i].is_dir) {
      if (full_name(child, sizeof(child), path, e[i].name, "")) count_tree(child, depth + 1, files, bytes);
    } else {
      (*files)++;
      *bytes += e[i].size;
    }
  }
}

/* Deletes everything below a drawer and the drawer itself. One entry at a time, listing again each time (deleting while
 * listing confuses ExNext). */
static int delete_tree(const char *path, int depth)
{
  struct FileInfoBlock *fib;
  BPTR lock;
  char name[64], child[NAME_LEN];
  int guard = 0;
  if (depth > 6) return 0;
  fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
  if (fib == NULL) return 0;
  for (;;) {
    int found = 0, is_dir = 0;
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lock == 0) break;
    if (Examine(lock, fib) && ExNext(lock, fib)) {
      found = 1;
      is_dir = fib->fib_DirEntryType > 0;
      strncpy(name, (const char *)fib->fib_FileName, sizeof(name) - 1);
      name[sizeof(name) - 1] = 0;
    }
    UnLock(lock);
    if (!found) break;
    if (!full_name(child, sizeof(child), path, name, "") || ++guard > 5000) {
      FreeDosObject(DOS_FIB, fib);
      return 0;
    }
    if (is_dir) {
      if (!delete_tree(child, depth + 1)) {
        FreeDosObject(DOS_FIB, fib);
        return 0;
      }
    } else {
      SetProtection((CONST_STRPTR)child, 0);
      if (!DeleteFile((CONST_STRPTR)child)) {
        FreeDosObject(DOS_FIB, fib);
        return 0;
      }
    }
  }
  FreeDosObject(DOS_FIB, fib);
  SetProtection((CONST_STRPTR)path, 0);
  return DeleteFile((CONST_STRPTR)path) != 0;
}

static int clean_paths(HubGame *g, const char *dest, char *old_dir, char *user_dir, int len)
{
  char dir[NAME_LEN], drawer[NAME_LEN];
  hub_game_dir(g, dir, sizeof(dir));
  if (dir[0] == 0 || g->flat) return 0;       /* a program in a shared drawer: nothing of its own to clean */
  if (!full_name(drawer, sizeof(drawer), dest, dir, "")) return 0;
  if (!full_name(old_dir, len, drawer, "old", "")) return 0;
  return full_name(user_dir, len, drawer, "user", "");
}

#define CLEAN_ENTRIES 700

int hub_clean_list(HubGame *g, const char *dest, HubCleanItem *items, int max)
{
  char old_dir[NAME_LEN], user_dir[NAME_LEN], child[NAME_LEN];
  DirEntry *e;
  int n, i, kind, count = 0;
  if (!clean_paths(g, dest, old_dir, user_dir, NAME_LEN)) return 0;
  e = (DirEntry *)hub_malloc(sizeof(DirEntry) * CLEAN_ENTRIES);
  if (e == NULL) return 0;
  for (kind = HUB_CLEAN_OLD; kind <= HUB_CLEAN_BUGS; kind++) {
    const char *base = kind == HUB_CLEAN_OLD ? old_dir : user_dir;
    n = list_dir(base, e, CLEAN_ENTRIES);
    for (i = 0; i < n && count < max; i++) {
      HubCleanItem *it;
      if (kind == HUB_CLEAN_BUGS && !bug_name(e[i].name)) continue;
      it = &items[count++];
      it->kind = kind;
      strncpy(it->name, e[i].name, sizeof(it->name) - 1);
      it->name[sizeof(it->name) - 1] = 0;
      it->files = 0;
      it->bytes = 0;
      if (e[i].is_dir) {
        if (full_name(child, sizeof(child), base, e[i].name, "")) count_tree(child, 1, &it->files, &it->bytes);
      } else {
        it->files = 1;
        it->bytes = e[i].size;
      }
    }
  }
  hub_free(e);
  return count;
}

int hub_clean_delete(HubGame *g, const char *dest, int what, char *err, int errlen)
{
  char old_dir[NAME_LEN], user_dir[NAME_LEN], child[NAME_LEN];
  DirEntry *e;
  int n, i, kind;
  if (!clean_paths(g, dest, old_dir, user_dir, NAME_LEN)) {
    snprintf(err, errlen, "No drawer of its own.");
    return 0;
  }
  e = (DirEntry *)hub_malloc(sizeof(DirEntry) * CLEAN_ENTRIES);
  if (e == NULL) {
    snprintf(err, errlen, "No memory.");
    return 0;
  }
  for (kind = HUB_CLEAN_OLD; kind <= HUB_CLEAN_BUGS; kind++) {
    const char *base = kind == HUB_CLEAN_OLD ? old_dir : user_dir;
    if (!(what & kind)) continue;
    n = list_dir(base, e, CLEAN_ENTRIES);
    for (i = 0; i < n; i++) {
      if (kind == HUB_CLEAN_BUGS && !bug_name(e[i].name)) continue;
      if (!full_name(child, sizeof(child), base, e[i].name, "")) continue;
      if (e[i].is_dir) {
        if (!delete_tree(child, 0)) {
          snprintf(err, errlen, "Cannot delete %s.", e[i].name);
          hub_free(e);
          return 0;
        }
      } else {
        SetProtection((CONST_STRPTR)child, 0);
        if (!DeleteFile((CONST_STRPTR)child)) {
          snprintf(err, errlen, "Cannot delete %s.", e[i].name);
          hub_free(e);
          return 0;
        }
      }
    }
  }
  hub_free(e);
  return 1;
}
