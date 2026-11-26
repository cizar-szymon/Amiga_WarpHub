/* hub_util - files, text and the settings of WARP HUB. */
#ifndef HUB_UTIL_H
#define HUB_UTIL_H

#include <exec/types.h>

/* The whole file, NUL-terminated (hub_free it); NULL when missing or larger
 * than max. len may be NULL. */
char *hub_read_file(const char *name, ULONG max, ULONG *len);

/* Writes name through name.tmp and a rename; 1 on success. */
int hub_write_file(const char *name, const void *data, ULONG len);

/* Makes every drawer of a full path that ends with a file name
 * ("PROGDIR:cache/images/x.png" -> cache, cache/images). */
void hub_make_dirs_for(const char *name);

/* UTF-8 -> what a Workbench font shows: Latin-1 (German letters stay),
 * the Polish letters without their marks (as the Polish texts of the ports). */
void hub_to_latin(const char *utf8, char *out, int outlen);

/* --- settings: PROGDIR:WarpHub.prefs ----------------------------------- *
 *   LANGUAGE pl|en|de
 *   CATALOG <https URL or file>
 *   GAMESDIR <drawer of the games>   APPSDIR <drawer of the programs>
 *   INSTALLED <key> <version> <drawer>        one line per game
 *   FAVORITES <key>,<key>...   the games marked with the star (right click)
 *   FILTER <group>             the group button chosen last              */

#define HUB_MAX_INSTALLED 32

typedef struct HubInstalled {
  char key[32];
  char version[24];
  char dir[256];
} HubInstalled;

typedef struct HubPrefs {
  char language[4];          /* "" = from the locale */
  char catalog[256];         /* "" = the default URL */
  char gamesdir[256];        /* the drawer of the installed games ("Work:Games") */
  char appsdir[256];         /* the drawer of the installed programs ("Work:Programs") */
  char priv[160];            /* the private source "host:port" ("" = none) */
  char priv_cert[65];        /* SHA-256 of its certificate, from the first login */
  char priv_pass[64];        /* its password, only when "remember" was chosen */
  HubInstalled inst[HUB_MAX_INSTALLED];
  int ninst;
  char favorites[1024];      /* "key,key" ("" = none) */
  char filter[16];           /* the group chosen last ("" = not yet) */
} HubPrefs;

void hub_prefs_load(HubPrefs *p, const char *file);
int hub_prefs_save(const HubPrefs *p, const char *file);
HubInstalled *hub_prefs_installed(HubPrefs *p, const char *key);
void hub_prefs_set_installed(HubPrefs *p, const char *key, const char *version,
                             const char *dir);
void hub_prefs_remove_installed(HubPrefs *p, const char *key);
int hub_prefs_favorite(const HubPrefs *p, const char *key);
/* adds the key to the favorites or takes it away: 1 when it is one now */
int hub_prefs_toggle_favorite(HubPrefs *p, const char *key);

/* --- remembered files: PROGDIR:WarpHub-cache/versions.txt, sums.txt ----- *
 * A value (the $VER of a game program, the SHA-256 of a cache file) kept
 * with the size and the date of the file: while they are the same the file
 * is not read again. Lines "<size> <days> <minute> <tick> <value> <name>". */

typedef struct HubStamp {
  ULONG size;
  LONG days, minute, tick;
  char value[68];
  char *name;
} HubStamp;

typedef struct HubStamps {
  HubStamp *e;
  int n, cap;
  int dirty;
} HubStamps;

void hub_stamps_load(HubStamps *t, const char *file);
/* writes only after a change; 1 when written or nothing to write */
int hub_stamps_save(HubStamps *t, const char *file);
void hub_stamps_free(HubStamps *t);
/* the value of name when the file has the remembered size and date (its
 * size in *size when not NULL), NULL when it changed, is not there or is not
 * known */
const char *hub_stamps_get(HubStamps *t, const char *name, ULONG *size);
/* remembers value with the size and the date the file has now */
void hub_stamps_set(HubStamps *t, const char *name, const char *value);

/* FNV-1a of a text: has the catalog changed (not a check of the content) */
ULONG hub_fnv(const char *data, ULONG len);

/* milliseconds of the clock (DateStamp, 20 ms steps): for differences only */
ULONG hub_ms(void);

#endif
