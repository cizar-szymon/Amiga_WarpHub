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
 *   INSTALLED <key> <version> <drawer>        one line per game        */

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
  HubInstalled inst[HUB_MAX_INSTALLED];
  int ninst;
} HubPrefs;

void hub_prefs_load(HubPrefs *p, const char *file);
int hub_prefs_save(const HubPrefs *p, const char *file);
HubInstalled *hub_prefs_installed(HubPrefs *p, const char *key);
void hub_prefs_set_installed(HubPrefs *p, const char *key, const char *version,
                             const char *dir);

#endif
