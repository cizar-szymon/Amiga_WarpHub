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

/* Downloads the needed files into <file>.upd, checks size and SHA-256,
 * and only when all came whole puts them in place; a program that is
 * replaced goes to <Game>/old/<name>-<old_version>. Returns 1 or 0 with err
 * (then nothing of the game was changed). */
int hub_install(HubGame *g, const char *dest, const char *old_version,
                HubProgress progress, void *user, char *err, int errlen);

/* The drawer of the game: the first part of its paths ("Settlers"). */
void hub_game_dir(HubGame *g, char *out, int outlen);
/* where a file of the catalog lies under the chosen drawer: its path, or for a
 * FLAT entry the path without "<Root>/" */
const char *hub_local_path(const HubGame *g, const char *path);

#endif
