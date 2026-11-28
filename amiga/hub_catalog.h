/* hub_catalog - catalog/catalog.txt (format: tools/make_catalog.py). */
#ifndef HUB_CATALOG_H
#define HUB_CATALOG_H

#include <exec/types.h>

enum { HUB_EN, HUB_PL, HUB_DE, HUB_LANGS };
#define HUB_MAX_README 8

typedef struct HubFile {
  const char *path;        /* relative to releases/amiga/ */
  ULONG size;
  const char *sha;         /* 64 hex digits */
  char flag;               /* 'N' replace, 'K' player's file: only if missing,
                              'X' not installed (texts for the window only) */
  char need;               /* set by hub_plan() */
  /* an install in progress (written by the network process, read by the
   * window "Downloaded files"): HUB_F_... and the bytes so far */
  volatile UBYTE state;
  volatile ULONG got;
} HubFile;

enum { HUB_F_NONE, HUB_F_WAIT, HUB_F_CHECK, HUB_F_GET, HUB_F_DONE, HUB_F_FAIL };

typedef struct HubGame {
  const char *key;
  const char *name[HUB_LANGS];
  const char *desc[HUB_LANGS];
  const char *repo;
  const char *branch;
  const char *version;
  const char *tags;        /* "pc,new" - groups of the filter buttons, or NULL */
  int local;               /* SOURCE local: repo not public yet (test catalog) */
  int priv;                /* SOURCE private: the full package from the private
                              source (files/<key>/<path> there, after a login) */
  int flat;                /* FLAT: the files of <Root>/ go straight into the
                              chosen drawer (a program such as WARP HUB) */
  const char *zip;
  ULONG zip_size;
  ULONG size;
  const char *image;       /* relative to the catalog ("images/x.png") or NULL */
  ULONG image_size;
  const char *image_sha;
  const char *notes[HUB_LANGS];
#define HUB_MAX_REQ 8
  const char *req[HUB_LANGS][HUB_MAX_REQ];   /* lines of the requirements */
  int nreq[HUB_LANGS];
  const char *readme[HUB_MAX_README];
  int readmes;
  HubFile *files;
  int nfiles;
} HubGame;

typedef struct HubCatalog {
  char *text;              /* the file; all strings point into it */
  HubGame *games;
  int ngames;
} HubCatalog;

/* Takes text (malloc'ed, NUL-terminated; freed by hub_catalog_free).
 * Returns NULL and fills err on a bad file. */
HubCatalog *hub_catalog_parse(char *text, char *err, int errlen);
void hub_catalog_free(HubCatalog *cat);
HubGame *hub_catalog_find(HubCatalog *cat, const char *key);

/* The text in lang, English when missing. */
const char *hub_text(const char *const *texts, int lang);

/* The game is in group tag (TAGS line). */
int hub_game_has_tag(const HubGame *g, const char *tag);
/* the files of the "how to start" guide: <Game>/doc/Install.txt when the
 * game has one, else the ReadMe.txt files of its empty data drawers */
int hub_guide_files(HubGame *g, HubFile **out, int max);

/* The file of the catalog with this path, or NULL. */
HubFile *hub_game_file(HubGame *g, const char *path);

/* "pl" / "de" / anything else -> HUB_PL / HUB_DE / HUB_EN */
int hub_lang(const char *name);

/* Letters, digits, _.+-&()', and single spaces inside a name; no "..". */
int hub_path_ok(const char *p);

#endif
