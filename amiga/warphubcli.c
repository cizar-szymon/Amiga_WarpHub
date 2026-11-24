/* WarpHubCLI - WARP HUB from the Shell: the catalog of the games and their
 * installation / update straight from GitHub (AmiSSL 5; WarpSSL on Warp
 * cards). The test bed of the network part before the window program.
 *
 *   WarpHubCLI LIST
 *   WarpHubCLI INFO <game> [LANG=pl|en|de]
 *   WarpHubCLI CHECK <game> TO=<drawer>       what an update would download
 *   WarpHubCLI INSTALL <game> TO=<drawer>     install or update
 *   WarpHubCLI UNINSTALL <game> TO=<drawer>   delete the files of the game (what the player made stays)
 *   WarpHubCLI TIME                           the clock and the SNTP time
 *
 *   CATALOG=<file or https URL>  another catalog (tests)
 *   CAFILE=<file>                extra trusted certificates (tests)
 *   NTP=<server>                 time server (pool.ntp.org)
 *   USENTP                       always take the time from NTP for the check
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hub_mem.h"
#include "hub_https.h"
#include "hub_time.h"
#include "hub_catalog.h"
#include "hub_install.h"

#define WARPHUB_VERSION "0.1"
#ifndef BUILD_UNIX
#define BUILD_UNIX 1795046400UL   /* 2026-11-19 */
#endif
#define CATALOG_URL \
  "https://raw.githubusercontent.com/cizar-szymon/Amiga_WarpHub/main/catalog/catalog.txt"
#define PREFS "PROGDIR:WarpHub.prefs"

const char version_tag[] = "$VER: WarpHubCLI " WARPHUB_VERSION " (19.11.2026)";

/* the stack also when started from an icon (PORTING_PLAN.md, phase 3) */
unsigned long __stack = 65536;
extern void __stkinit(void);
void (*const warphub_stackSwap)(void) = __stkinit;

static char err[256];

/* --- a whole body into memory ------------------------------------------- */

typedef struct Mem {
  char *data;
  ULONG len, cap;
} Mem;

static int mem_sink(void *user, const UBYTE *data, LONG len)
{
  Mem *m = (Mem *)user;
  if (m->len + len + 1 > m->cap) {
    ULONG cap = m->cap ? m->cap * 2 : 65536;
    char *d;
    while (cap < m->len + len + 1) cap *= 2;
    if (cap > (8UL << 20)) return 0;
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

static char *read_url(const char *url)
{
  char host[128], path[512];
  const char *p;
  int i;
  HubConn *c;
  Mem m = {NULL, 0, 0};
  int status;
  if (strncmp(url, "https://", 8) != 0) {
    snprintf(err, sizeof(err), "Only https:// URLs.");
    return NULL;
  }
  p = url + 8;
  for (i = 0; *p && *p != '/' && i < (int)sizeof(host) - 1; i++) host[i] = *p++;
  host[i] = 0;
  hub_url_path(path, sizeof(path), *p ? p : "/");
  c = hub_conn_new(host, 443);
  if (c == NULL) {
    snprintf(err, sizeof(err), "No memory.");
    return NULL;
  }
  status = hub_get(c, path, mem_sink, &m, NULL, 0, err, sizeof(err));
  hub_conn_free(c);
  if (status != 200) {
    if (status > 0) snprintf(err, sizeof(err), "%s: HTTP %d.", url, status);
    hub_free(m.data);
    return NULL;
  }
  if (m.data == NULL) mem_sink(&m, (const UBYTE *)"", 0);
  return m.data;
}

static char *read_file(const char *name)
{
  BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
  Mem m = {NULL, 0, 0};
  UBYTE b[4096];
  LONG n;
  if (fh == 0) {
    snprintf(err, sizeof(err), "Cannot open %s.", name);
    return NULL;
  }
  while ((n = Read(fh, b, sizeof(b))) > 0) {
    if (!mem_sink(&m, b, n)) {
      Close(fh);
      hub_free(m.data);
      snprintf(err, sizeof(err), "%s too large.", name);
      return NULL;
    }
  }
  Close(fh);
  if (m.data == NULL) mem_sink(&m, (const UBYTE *)"", 0);
  return m.data;
}

/* --- installed versions: PROGDIR:WarpHub.prefs, "INSTALLED <key> <version> <drawer>" */

static int installed_version(const char *key, const char *dest, char *out, int outlen)
{
  char *text = read_file(PREFS), *line, *next;
  int found = 0;
  out[0] = 0;
  if (text == NULL) return 0;
  for (line = text; line && *line; line = next) {
    char k[64], v[32], d[300];
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    if (sscanf(line, "INSTALLED %63s %31s %299[^\n]", k, v, d) == 3 &&
        strcmp(k, key) == 0 && (dest == NULL || stricmp(d, dest) == 0)) {
      strncpy(out, v, outlen - 1);
      out[outlen - 1] = 0;
      found = 1;
    }
  }
  hub_free(text);
  return found;
}

static void remember(const char *key, const char *version, const char *dest)
{
  char *text = read_file(PREFS), *line, *next;
  BPTR fh = Open((CONST_STRPTR)PREFS ".new", MODE_NEWFILE);
  if (fh == 0) {
    hub_free(text);
    return;
  }
  for (line = text; line && *line; line = next) {
    char k[64], v[32], d[300];
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    if (sscanf(line, "INSTALLED %63s %31s %299[^\n]", k, v, d) == 3 &&
        strcmp(k, key) == 0 && stricmp(d, dest) == 0) {
      continue;
    }
    FPuts(fh, (CONST_STRPTR)line);
    FPuts(fh, (CONST_STRPTR)"\n");
  }
  FPrintf(fh, (CONST_STRPTR)"INSTALLED %s %s %s\n", (LONG)key, (LONG)version, (LONG)dest);
  Close(fh);
  hub_free(text);
  DeleteFile((CONST_STRPTR)PREFS);
  Rename((CONST_STRPTR)PREFS ".new", (CONST_STRPTR)PREFS);
}

/* --- time ------------------------------------------------------------------ */

static ULONG ms_now(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return (ULONG)ds.ds_Minute * 60000UL + (ULONG)ds.ds_Tick * 20UL;
}

static void show_date(const char *what, ULONG unix_time)
{
  /* days -> y-m-d (civil from days, Howard Hinnant) */
  long z = (long)(unix_time / 86400UL) + 719468L;
  long era = z / 146097L, doe = z - era * 146097L;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1;
  long m = mp < 10 ? mp + 3 : mp - 9;
  ULONG s = unix_time % 86400UL;
  printf("%s %04ld-%02ld-%02ld %02lu:%02lu:%02lu\n", what, y + (m <= 2), m, d,
         s / 3600, (s / 60) % 60, s % 60);
}

/* time servers tried one after another (UDP sometimes gets lost) */
static const char *const default_ntp[] = {
  "pool.ntp.org", "time.cloudflare.com", "time.google.com", "europe.pool.ntp.org", NULL
};

/* the time for the certificate check: a time server first, the clock of the
 * Amiga only when none answers and the clock looks right (HUB_CLOCK_OK) */
static int set_verify_time(const char *ntp, int force_ntp, int verbose)
{
  ULONG now = hub_amiga_now();
  ULONG t;
  (void)force_ntp;                   /* USENTP: always now, kept for scripts */
  t = hub_sntp(ntp, err, sizeof(err));
  if (t == 0 && ntp == default_ntp[0]) {
    int i;
    for (i = 1; t == 0 && default_ntp[i] != NULL; i++) {
      t = hub_sntp(default_ntp[i], err, sizeof(err));
    }
  }
  if (t == 0) {
    if (HUB_CLOCK_OK(now)) return 1;
    show_date("Clock of the Amiga:", now);
    printf("The clock of this Amiga is wrong and the time server does not\n"
           "answer (%s). Set the date (Prefs/Time) and try again.\n", err);
    return 0;
  }
  if (verbose || !HUB_CLOCK_OK(now)) {
    show_date("Clock of the Amiga:", now);
    show_date("Time from NTP:     ", t);
    printf("(used only to check certificates; the clock is not changed)\n");
  }
  hub_https_verify_time(t);
  return 1;
}

/* --- commands ----------------------------------------------------------- */

static void progress(void *user, const HubFile *f, ULONG done, ULONG total)
{
  static const HubFile *last = NULL;
  (void)user;
  if (f != last) {
    printf("  %s (%lu bytes)\n", f->path, f->size);
    last = f;
  }
  (void)done;
  (void)total;
}

static void print_kb(ULONG bytes)
{
  if (bytes >= 1048576UL) {
    printf("%lu.%lu MB", bytes / 1048576UL, (bytes % 1048576UL) * 10 / 1048576UL);
  } else {
    printf("%lu KB", (bytes + 1023) / 1024);
  }
}

int main(void)
{
  static const char tmpl[] =
      "CMD/A,GAME,TO/K,LANG/K,CATALOG/K,CAFILE/K,NTP/K,USENTP/S";
  enum { A_CMD, A_GAME, A_TO, A_LANG, A_CATALOG, A_CAFILE, A_NTP, A_USENTP, A_N };
  LONG args[A_N];
  struct RDArgs *rd;
  const char *cmd, *ntp, *catalog_src;
  char *text;
  HubCatalog *cat = NULL;
  int lang, rc = RETURN_FAIL, i;

  memset(args, 0, sizeof(args));
  rd = ReadArgs((CONST_STRPTR)tmpl, args, NULL);
  if (rd == NULL) {
    PrintFault(IoErr(), (CONST_STRPTR)"WarpHubCLI");
    printf("Usage: WarpHubCLI LIST | INFO <game> | CHECK <game> TO=<drawer> |\n"
           "       INSTALL <game> TO=<drawer> | UNINSTALL <game> TO=<drawer> | TIME   [LANG=pl|en|de]\n");
    return RETURN_FAIL;
  }
  cmd = (const char *)args[A_CMD];
  ntp = args[A_NTP] ? (const char *)args[A_NTP] : default_ntp[0];
  lang = hub_lang((const char *)args[A_LANG]);
  printf("WARP HUB %s (Shell)\n", WARPHUB_VERSION);

  if (!hub_https_init((const char *)args[A_CAFILE], err, sizeof(err))) {
    printf("%s\n", err);
    FreeArgs(rd);
    return RETURN_FAIL;
  }
  if (stricmp(cmd, "TIME") == 0) {
    ULONG t = hub_sntp(ntp, err, sizeof(err));
    show_date("Clock of the Amiga:", hub_amiga_now());
    if (t) show_date("Time from NTP:     ", t); else printf("%s\n", err);
    rc = t ? RETURN_OK : RETURN_WARN;
    goto out;
  }
  if (!set_verify_time(ntp, args[A_USENTP] != 0, 0)) goto out;

  catalog_src = args[A_CATALOG] ? (const char *)args[A_CATALOG] : CATALOG_URL;
  {
    ULONG t0 = ms_now();
    text = strncmp(catalog_src, "https://", 8) == 0 ? read_url(catalog_src)
                                                    : read_file(catalog_src);
    if (text == NULL) {
      printf("Catalog: %s\n", err);
      goto out;
    }
    cat = hub_catalog_parse(text, err, sizeof(err));
    if (cat == NULL) {
      printf("%s\n", err);
      goto out;
    }
    if (strncmp(catalog_src, "https://", 8) == 0) {
      printf("Catalog: %d games, %lu ms, %s\n", cat->ngames, ms_now() - t0,
             hub_conn_cipher(NULL));
    }
  }

  if (stricmp(cmd, "LIST") == 0) {
    for (i = 0; i < cat->ngames; i++) {
      HubGame *g = &cat->games[i];
      char have[32];
      printf("%-12s %-8s ", g->key, g->version);
      print_kb(g->size);
      printf("  %s", hub_text(g->name, lang));
      if (installed_version(g->key, NULL, have, sizeof(have))) printf("  [installed %s]", have);
      printf("\n");
    }
    rc = RETURN_OK;
  } else if (args[A_GAME] == 0) {
    printf("%s needs a game (see LIST).\n", cmd);
  } else {
    HubGame *g = hub_catalog_find(cat, (const char *)args[A_GAME]);
    const char *dest = (const char *)args[A_TO];
    if (g == NULL) {
      printf("No game %s in the catalog.\n", (const char *)args[A_GAME]);
    } else if (stricmp(cmd, "INFO") == 0) {
      printf("%s %s\n%s\n", hub_text(g->name, lang), g->version, hub_text(g->desc, lang));
      printf("github.com/%s (%s), %d files, ", g->repo, g->branch, g->nfiles);
      print_kb(g->size);
      printf("\n");
      if (hub_text(g->notes, lang)[0]) printf("History of releases: %s\n", hub_text(g->notes, lang));
      for (i = 0; i < g->readmes; i++) printf("To add yourself: %s\n", g->readme[i]);
      rc = RETURN_OK;
    } else if (dest == NULL) {
      printf("%s needs TO=<drawer> (e.g. TO=Work:Games).\n", cmd);
    } else if (stricmp(cmd, "CHECK") == 0 || stricmp(cmd, "INSTALL") == 0) {
      HubPlan plan;
      char dir[64], have[32];
      ULONG t0 = ms_now();
      hub_game_dir(g, dir, sizeof(dir));
      installed_version(g->key, dest, have, sizeof(have));
      hub_plan(g, dest, &plan);
      printf("%s %s -> %s/%s%s%s: %d of %d files to download (", hub_text(g->name, lang),
             g->version, dest, dir, have[0] ? ", installed " : "", have, plan.files, g->nfiles);
      print_kb(plan.bytes);
      printf("), %d of the player kept; checked in %lu ms\n", plan.kept, ms_now() - t0);
      if (stricmp(cmd, "CHECK") == 0) {
        for (i = 0; i < g->nfiles; i++) {
          if (g->files[i].need) printf("  %s\n", g->files[i].path);
        }
        rc = RETURN_OK;
      } else if (plan.files == 0) {
        printf("Up to date.\n");
        remember(g->key, g->version, dest);
        rc = RETURN_OK;
      } else {
        t0 = ms_now();
        if (hub_install(g, dest, have, progress, NULL, err, sizeof(err))) {
          ULONG ms = ms_now() - t0;
          printf("Done: ");
          print_kb(plan.bytes);
          printf(" in %lu.%lu s", ms / 1000, (ms % 1000) / 100);
          if (ms > 0) printf(", %lu KB/s", (ULONG)(((plan.bytes / 1024UL) * 1000UL) / ms));
          printf(", %s\n", hub_conn_cipher(NULL));
          remember(g->key, g->version, dest);
          for (i = 0; i < g->readmes; i++) {
            printf("Add yourself: see %s/%s\n", dest, g->readme[i]);
          }
          rc = RETURN_OK;
        } else {
          printf("Not installed: %s\nNothing of the game was changed.\n", err);
        }
      }
    } else if (stricmp(cmd, "UNINSTALL") == 0) {
      int removed = 0, left = 0;
      if (hub_uninstall(g, dest, &removed, &left, err, sizeof(err))) {
        printf("Uninstalled %s: %d files deleted%s.\n", hub_text(g->name, lang), removed,
               left ? ", the drawer of the game still holds your own files" : "");
        rc = RETURN_OK;
      } else {
        printf("Not uninstalled: %s\n", err);
      }
    } else {
      printf("Unknown command %s.\n", cmd);
    }
  }

out:
  hub_catalog_free(cat);
  hub_https_cleanup();
  FreeArgs(rd);
  return rc;
}
