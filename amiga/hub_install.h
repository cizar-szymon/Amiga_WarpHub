/* hub_install - install / update a game from GitHub, file by file. */
#ifndef HUB_INSTALL_H
#define HUB_INSTALL_H

#include "hub_catalog.h"
#include "hub_https.h"

typedef struct HubPlan {
  int files;               /* to download */
  ULONG bytes;
  int kept;                /* player's files left as they are */
} HubPlan;

/* Progress: the file being downloaded, bytes of all so far. */
typedef void (*HubProgress)(void *user, const HubFile *f, ULONG done, ULONG total);

/* Marks f->need for every file: missing, other size or other SHA-256
 * ("K" files only when missing). dest: the drawer that gets <Game>.info and
 * <Game>/ (e.g. "Work:Games"). */
void hub_plan(HubGame *g, const char *dest, HubPlan *plan);

/* The same with progress: called about every 256 KB of the file that is being read, and when a file starts.
 * index/count: the file and the number of files that are read; all_done/all_total: bytes of all of them.
 * Ctrl-C / the Stop button (hub_break) ends it with plan->files = -1. */
typedef void (*HubCheckProgress)(void *user, const HubFile *f, int index, int count,
                                 ULONG all_done, ULONG all_total);
void hub_plan_progress(HubGame *g, const char *dest, HubPlan *plan, HubCheckProgress cb, void *user);

/* Downloads the needed files into <file>.upd, checks size and SHA-256,
 * and only when all came whole puts them in place; a program that is
 * replaced goes to <Game>/old/<name>-<old_version>. Returns 1 or 0 with err
 * (then nothing of the game was changed). */
int hub_install(HubGame *g, const char *dest, const char *old_version,
                HubProgress progress, void *user, char *err, int errlen);

/* The private source for games with SOURCE private (host, port, SHA-256 of
 * its certificate, password); host "" = none. */
void hub_install_private(const char *host, int port, const char *pin, const char *password);

/* The drawer of the game: the first part of its paths ("Settlers"). */
void hub_game_dir(HubGame *g, char *out, int outlen);
/* where a file of the catalog lies under the chosen drawer: its path, or for a
 * FLAT entry the path without "<Root>/" */
const char *hub_local_path(const HubGame *g, const char *path);

/* Deletes the files of the game that the catalog lists, then the drawers that
 * became empty. What the player made (saved games, ini files, reports, old/)
 * stays. *left = 1 when the drawer of the game still holds something.
 * Returns 1, or 0 with err. */
int hub_uninstall(HubGame *g, const char *dest, int *removed, int *left,
                  char *err, int errlen);

/* What a game leaves in its drawer that WARP HUB can offer to delete (in a window of its own): the old versions
 * (<Game>/old/) and the bug reports (<Game>/user/bugNNN/ and the files bugNNN.* of older releases). The saved games
 * and the settings are never touched. */
typedef struct HubCleanItem {
  int kind;                   /* HUB_CLEAN_OLD or HUB_CLEAN_BUGS */
  char name[64];
  int files;
  ULONG bytes;
} HubCleanItem;
#define HUB_CLEAN_OLD 1
#define HUB_CLEAN_BUGS 2
/* The items (one per entry of old/, per bug drawer, per loose bug file), at most max; 0 when there are none or the game has
 * no drawer of its own (a program in a shared drawer). */
int hub_clean_list(HubGame *g, const char *dest, HubCleanItem *items, int max);
/* Deletes the items of the kinds in 'what' (HUB_CLEAN_*). Returns 1, or 0 with err. */
int hub_clean_delete(HubGame *g, const char *dest, int what, char *err, int errlen);

#endif
