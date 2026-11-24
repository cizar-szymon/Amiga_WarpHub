/* hub_worker - the network process of WARP HUB: the window never waits.
 *
 * The window fills the job fields of HubWork and calls hub_worker_go(); the
 * process does the job and signals the window (progress and the end). Only
 * the network process opens bsdsocket.library and AmiSSL (a socket base
 * belongs to one process). */
#ifndef HUB_WORKER_H
#define HUB_WORKER_H

#include <exec/types.h>
#include "hub_catalog.h"
#include "hub_install.h"

enum { HUB_JOB_NONE, HUB_JOB_REFRESH, HUB_JOB_INSTALL, HUB_JOB_PROBE, HUB_JOB_QUIT };

/* the private source after a job */
enum { HUB_PRIV_NONE, HUB_PRIV_OK, HUB_PRIV_BAD_PASSWORD, HUB_PRIV_BLOCKED,
       HUB_PRIV_ERROR, HUB_PRIV_PROBED };

typedef struct HubWork {
  /* the job (written by the window while the process is idle) */
  int job;
  char key[32];              /* install: the game */
  char dir[256];             /* install: the drawer that gets <Game>/ */
  char old_version[24];      /* install: the installed version (old/<prog>-<v>) */
  char catalog_src[256];     /* refresh: https URL or a file */
  char cache[64];            /* "PROGDIR:cache" */
  HubCatalog *cat;           /* install: the catalog of the window */
  /* the private source ("" = none): refresh adds its catalog, install takes
   * SOURCE private games from it; probe only connects and gives priv_peer */
  char priv_host[128];
  int priv_port;
  char priv_pin[65];         /* SHA-256 of its certificate (64 hex) */
  char priv_pass[64];

  /* the state (written by the process) */
  volatile int busy;
  volatile int finished;     /* the job that ended; the window sets it back to 0 */
  int ok;
  char msg[256];
  volatile ULONG done, total;
  volatile int loading_games; /* refresh: the pictures and texts of the games are being loaded (chk_*) */
  volatile int checking;     /* install: the files are being checked (chk_*), not downloaded */
  volatile int chk_index, chk_count;
  char file[128];
  HubCatalog *new_cat;       /* refresh: the new catalog, the window takes it */
  HubPlan plan;              /* install: what was downloaded */
  char net_info[160];        /* TLS, time from NTP ... */
  int priv_status;           /* HUB_PRIV_... */
  char priv_peer[65];        /* probe: SHA-256 of the certificate of the server */
  char priv_msg[160];        /* why the private source failed */
} HubWork;

/* Starts the process; signal: the bit the window waits on. 0 on failure. */
int hub_worker_start(HubWork *w, int signal);
void hub_worker_go(HubWork *w);
/* Stops the job in progress (as Ctrl-C). */
void hub_worker_break(void);
/* Ends the process and waits for it. */
void hub_worker_quit(HubWork *w);

/* The cache files of a game (the window reads them). */
void hub_cache_image(const HubWork *w, const char *key, char *out, int outlen);
void hub_cache_notes(const HubWork *w, const char *key, int lang, char *out, int outlen);
void hub_cache_guide(const HubWork *w, const char *key, int n, char *out, int outlen);

#endif
