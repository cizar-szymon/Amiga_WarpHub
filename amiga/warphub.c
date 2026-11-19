/* WARP HUB - the games of the WARP ports for the Workbench: a gallery with
 * pictures, the page of a game with its history of releases, installation
 * and update straight from GitHub (AmiSSL 5; WarpSSL on Warp cards).
 *
 * ReAction window (OS 3.2) with two space.gadget areas drawn here: the
 * gallery of tiles and the page of the chosen game. The network runs in a
 * process of its own (hub_worker.c), so the window never waits.
 *
 * Shell / Tool Types: CATALOG=<https URL or file> LANG=pl|en|de */

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <datatypes/pictureclass.h>
#include <libraries/asl.h>
#include <libraries/locale.h>
#include <workbench/startup.h>
#include <utility/hooks.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <proto/asl.h>
#include <proto/locale.h>
#include <proto/icon.h>
#include <clib/alib_protos.h>

#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/space.h>
#include <proto/scroller.h>
#include <proto/fuelgauge.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <gadgets/space.h>
#include <gadgets/scroller.h>
#include <gadgets/fuelgauge.h>
#include <reaction/reaction_macros.h>   /* RA_OpenWindow, RA_HandleInput */

#include <stdio.h>
#include <string.h>

#include "hub_mem.h"
#include "hub_util.h"
#include "hub_catalog.h"
#include "hub_worker.h"

extern struct WBStartup *_WBenchMsg;

#define WARPHUB_VERSION "0.2"
const char version_tag[] = "$VER: WarpHub " WARPHUB_VERSION " (19.11.2026)";

unsigned long __stack = 65536;
extern void __stkinit(void);
void (*const warphub_stackSwap)(void) = __stkinit;

#define CATALOG_URL \
  "https://raw.githubusercontent.com/cizar-szymon/Amiga_WarpHub/main/catalog/catalog.txt"
#define PREFS_FILE "PROGDIR:WarpHub.prefs"
#define CACHE_DIR "PROGDIR:cache"

/* the tile: 224x140 (the size of the pictures) on big screens, 144x90 (the
 * picture scaled by the datatype) below 1024x700, so that 640x480 fits */
static int TILE_W = 224, TILE_H = 140;
#define GAP 14

/* initialised: as common symbols they would pull the libnix auto-open stubs
 * ("window.library failed to load") */
struct Library *WindowBase = NULL, *LayoutBase = NULL, *ButtonBase = NULL,
    *ChooserBase = NULL, *ListBrowserBase = NULL, *SpaceBase = NULL, *ScrollerBase = NULL,
    *FuelGaugeBase = NULL;

/* --- texts --------------------------------------------------------------- */

enum {
  T_REFRESH, T_INSTALL, T_UPDATE, T_REPAIR, T_STOP, T_INSTALLED, T_NEW_VERSION,
  T_AVAILABLE, T_SIZE, T_NEEDS_DATA, T_CHOOSE_DIR, T_ASK_INSTALL, T_ASK_UPDATE, T_YES_NO,
  T_DONE, T_FAILED, T_LOADING, T_CACHE_ONLY, T_DOWNLOADING, T_UP_TO_DATE, T_HISTORY_NONE,
  T_READY, T_CHECKING, T_OK, T_NO_CATALOG, T_ADD_YOURSELF, T_FILES, T_IN, T_NO_NET, T_SOON,
  T_NOT_PUBLIC, T_EMPTY, T_NEW_SHORT, T_REQ, T_GAMESDIR, T_CHOOSE_GAMESDIR, T_FOUND, T_SCANNING, T_APPSDIR, T_CHOOSE_APPSDIR, T_GUIDE, T_GUIDE_NONE,
  T_GUIDE_MISSING, T_PUT_IN, T_CLOSE,
  T_COUNT
};

static const char *const texts[3][T_COUNT] = {
  { /* en */
    "Refresh", "Install", "Update", "Check files", "Stop", "Installed %s",
    "New version %s (installed %s)", "Available", "%s, %ld files",
    "Needs files of the original game: see ReadMe.txt in the drawer of the game.",
    "Drawer for %s (the game gets its own drawer in it)",
    "Install %s %s\ninto %s?\n\n%s to download.", "Update %s to %s\nin %s?\n\n"
    "Only changed files are downloaded; the old program goes to old/.\n"
    "Your settings and Tool Types stay.", "Yes|No",
    "%s %s is installed in %s.", "Not installed: %s", "Loading the catalog...",
    "Catalog from the cache (no connection: %s)", "Downloading %s (%ld%%)",
    "Up to date.", "No history of releases yet.", "%d games. %s", "Checking files of %s...",
    "OK", "No catalog yet - press Refresh.", "Add yourself:", "files", "in",
    "No network: %s", "Soon",
    "Test: the repo of this game is not public yet - it can be installed after the release.",
    "No games in this group yet.", "New: %s", "Requirements",
    "Games drawer", "Drawer of your installed games",
    "%d games found in %s.", "Looking for your games...",
    "Programs drawer", "Drawer of your installed programs",
    "How to start", "Nothing to add: the game is ready to start after the installation.",
    "The guide is not downloaded yet - press Refresh.", "Into the drawer %s:", "Close"
  },
  { /* pl */
    "Odswiez", "Zainstaluj", "Aktualizuj", "Sprawdz pliki", "Przerwij", "Zainstalowana %s",
    "Nowa wersja %s (masz %s)", "Do pobrania", "%s, plikow: %ld",
    "Potrzebne pliki oryginalnej gry: zobacz ReadMe.txt w szufladzie gry.",
    "Szuflada dla %s (gra dostanie w niej wlasna szuflade)",
    "Zainstalowac %s %s\nw %s?\n\nDo pobrania: %s.", "Zaktualizowac %s do %s\nw %s?\n\n"
    "Pobrane zostana tylko zmienione pliki; stary program trafi do old/.\n"
    "Twoje ustawienia i Tool Types zostaja.", "Tak|Nie",
    "%s %s zainstalowana w %s.", "Nie zainstalowano: %s", "Wczytywanie katalogu...",
    "Katalog z pamieci (brak polaczenia: %s)", "Pobieranie %s (%ld%%)",
    "Aktualna.", "Brak historii zmian.", "Gier: %d. %s", "Sprawdzanie plikow %s...",
    "OK", "Brak katalogu - nacisnij Odswiez.", "Dodaj sam:", "plikow", "w",
    "Brak sieci: %s", "Wkrotce",
    "Test: repo tej gry nie jest jeszcze publiczne - instalacja bedzie mozliwa po publikacji.",
    "Na razie brak gier w tej grupie.", "Nowa wersja %s", "Wymagania",
    "Katalog gier", "Szuflada z zainstalowanymi grami",
    "Znalezione gry: %d w %s.", "Szukam zainstalowanych gier...",
    "Katalog programow", "Szuflada z zainstalowanymi programami",
    "Instrukcja uruchomienia", "Nic nie trzeba dokladac: po instalacji gra jest gotowa do uruchomienia.",
    "Instrukcja nie jest jeszcze pobrana - nacisnij Odswiez.", "Do szuflady %s:", "Zamknij"
  },
  { /* de */
    "Aktualisieren", "Installieren", "Update", "Dateien pr\xfc" "fen", "Abbrechen",
    "Installiert %s", "Neue Version %s (installiert %s)", "Verf\xfc" "gbar", "%s, %ld Dateien",
    "Braucht Dateien des Originalspiels: siehe ReadMe.txt in der Schublade des Spiels.",
    "Schublade f\xfc" "r %s (das Spiel bekommt darin eine eigene Schublade)",
    "%s %s\nnach %s installieren?\n\n%s herunterladen.", "%s auf %s\nin %s aktualisieren?\n\n"
    "Nur ge\xe4nderte Dateien werden geladen; das alte Programm kommt nach old/.\n"
    "Deine Einstellungen und Tool Types bleiben.", "Ja|Nein",
    "%s %s ist in %s installiert.", "Nicht installiert: %s", "Katalog wird geladen...",
    "Katalog aus dem Cache (keine Verbindung: %s)", "Lade %s (%ld%%)",
    "Aktuell.", "Noch keine Versionsgeschichte.", "%d Spiele. %s", "Pr\xfc" "fe Dateien von %s...",
    "OK", "Noch kein Katalog - Aktualisieren dr\xfc" "cken.", "Selbst hinzuf\xfc" "gen:",
    "Dateien", "in", "Kein Netz: %s", "Bald",
    "Test: das Repo dieses Spiels ist noch nicht \xf6" "ffentlich - Installation nach der Ver\xf6" "ffentlichung.",
    "Noch keine Spiele in dieser Gruppe.", "Neu: %s", "Anforderungen",
    "Spiele-Schublade", "Schublade der installierten Spiele",
    "%d Spiele in %s gefunden.", "Suche installierte Spiele...",
    "Programm-Schublade", "Schublade der installierten Programme",
    "Startanleitung", "Nichts hinzuzuf\xfc" "gen: das Spiel ist nach der Installation startbereit.",
    "Die Anleitung ist noch nicht geladen - Aktualisieren dr\xfc" "cken.", "In die Schublade %s:",
    "Schlie\xdf" "en"
  }
};

static int lang = HUB_EN;

/* the filter buttons: the group (TAGS of the catalog) and its label */
#define FILTERS 8
static const char *const filter_tag[FILTERS] = {
  "", "apps", "amiga", "pc", "mac", "warpextreme", "arcade", "other"
};
static const char *const filter_label[3][FILTERS] = {
  {"All", "Programs", "AMIGA port", "PC port", "MAC port", "WarpExtreme", "Arcade", "Other"},
  {"Wszystkie", "Programy", "AMIGA port", "PC port", "MAC port", "WarpExtreme", "Arcade", "Inne"},
  {"Alle", "Programme", "AMIGA-Port", "PC-Port", "MAC-Port", "WarpExtreme", "Arcade", "Andere"}
};
static int filter = 0;
static int view[128], nview;       /* the games shown (indices into the catalog) */
/* tests from the Shell: install without requesters after the first refresh */
static char test_key[32], test_dir[256], test_gamesdir[256];
#define TX(i) (texts[lang == HUB_PL ? 1 : lang == HUB_DE ? 2 : 0][i])

/* --- state --------------------------------------------------------------- */

enum { GID_LANG = 1, GID_REFRESH, GID_GALLERY, GID_SCROLL, GID_PAGE, GID_INSTALL, GID_STOP,
       GID_NOTES, GID_PROGRESS, GID_REQ, GID_GAMESDIR, GID_APPSDIR, GID_UPDATE, GID_GUIDE, GID_FILTER };     /* GID_FILTER + 0..FILTERS-1 */

typedef struct Tile {
  Object *dt;                /* the picture (datatype), NULL = none */
  struct BitMap *bm;
  int w, h;
} Tile;

static HubPrefs prefs;
static HubWork work;
static HubCatalog *cat;
static Tile *tiles;
static int selected = -1, top_row = 0, cols = 1, rows_visible = 1;
static char status_text[300];

static struct Screen *scr;
static struct Window *win;
static Object *win_obj, *g_lang, *g_refresh, *g_gallery, *g_scroll, *g_page, *g_install,
    *g_stop, *g_notes, *g_req, *g_gamesdir, *g_appsdir, *g_update, *g_guide, *g_filter[FILTERS];
static struct List notes_list, lang_list;
static struct MsgPort *app_port;
static struct Hook gallery_hook, page_hook, req_hook;
static BYTE work_signal = -1;

enum { P_BG, P_TILE, P_TEXT, P_DIM, P_ACCENT, P_GREEN, P_SEL, P_COUNT };
static LONG pens[P_COUNT];
static const ULONG pen_rgb[P_COUNT] = {
  0x1b1e27, 0x2a2f3d, 0xf0f0f4, 0x9aa1b2, 0xf08c28, 0x5cc85c, 0xf0b040
};

/* --- small helpers -------------------------------------------------------- */

static void latin(const char *utf8, char *out, int outlen)
{
  hub_to_latin(utf8 != NULL ? utf8 : "", out, outlen);
}

static void size_text(ULONG bytes, char *out, int outlen)
{
  if (bytes >= 1048576UL) {
    snprintf(out, outlen, "%lu.%lu MB", bytes / 1048576UL, (bytes % 1048576UL) * 10 / 1048576UL);
  } else {
    snprintf(out, outlen, "%lu KB", (bytes + 1023) / 1024);
  }
}

/* -1 a<b, 0 equal, 1 a>b (numbers split by dots) */
static int version_cmp(const char *a, const char *b)
{
  while (*a || *b) {
    long x = 0, y = 0;
    while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
    while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
    if (x != y) return x < y ? -1 : 1;
    if (*a == '.') a++;
    if (*b == '.') b++;
    if ((*a && !(*a >= '0' && *a <= '9')) || (*b && !(*b >= '0' && *b <= '9'))) break;
  }
  return 0;
}

static HubInstalled *installed(HubGame *g)
{
  return hub_prefs_installed(&prefs, g->key);
}

/* no status bar (the user wants none): what is going on - downloading with
 * its percentage, a missing connection, the result - is in the window title,
 * "WARP HUB" alone when there is nothing to say */
static char title_text[340];

static void set_status(const char *text)
{
  strncpy(status_text, text, sizeof(status_text) - 1);
  status_text[sizeof(status_text) - 1] = 0;
  if (status_text[0]) snprintf(title_text, sizeof(title_text), "WARP HUB - %s", status_text);
  else strcpy(title_text, "WARP HUB");
  if (win != NULL) SetWindowTitles(win, (UBYTE *)title_text, (UBYTE *)~0);
}

static void set_progress(ULONG done, ULONG total)
{
  (void)done;
  (void)total;
  set_status(status_text);      /* the percentage is in the text */
}

static void redraw(Object *o)
{
  if (win != NULL) RefreshGList((struct Gadget *)o, win, NULL, 1);
}

/* --- pictures -------------------------------------------------------------- */

static void free_tiles(void)
{
  int i;
  if (tiles == NULL || cat == NULL) return;
  for (i = 0; i < cat->ngames; i++) {
    if (tiles[i].dt != NULL) DisposeDTObject(tiles[i].dt);
  }
  hub_free(tiles);
  tiles = NULL;
}

static void load_tiles(void)
{
  int i;
  char name[300];
  free_tiles();
  if (cat == NULL) return;
  tiles = (Tile *)hub_calloc(cat->ngames + 1, sizeof(Tile));
  if (tiles == NULL || scr == NULL) return;
  for (i = 0; i < cat->ngames; i++) {
    Tile *t = &tiles[i];
    struct BitMapHeader *bmh = NULL;
    hub_cache_image(&work, cat->games[i].key, name, sizeof(name));
    t->dt = NewDTObject((APTR)name, DTA_GroupID, GID_PICTURE, PDTA_Remap, TRUE,
                        PDTA_Screen, (ULONG)scr, PDTA_DestMode, PMODE_V43,
                        OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    if (t->dt == NULL) continue;
    if (TILE_W != 224) {
      struct pdtScale ps;
      ps.MethodID = PDTM_SCALE;
      ps.ps_NewWidth = TILE_W;
      ps.ps_NewHeight = TILE_H;
      ps.ps_Flags = 0;
      DoMethodA(t->dt, (Msg)&ps);
    }
    if (DoDTMethod(t->dt, NULL, NULL, DTM_PROCLAYOUT, NULL, TRUE) == 0 ||
        GetDTAttrs(t->dt, PDTA_DestBitMap, (ULONG)&t->bm, PDTA_BitMapHeader,
                   (ULONG)&bmh, TAG_DONE) < 2 || t->bm == NULL || bmh == NULL) {
      DisposeDTObject(t->dt);
      t->dt = NULL;
      t->bm = NULL;
      continue;
    }
    t->w = bmh->bmh_Width < TILE_W ? bmh->bmh_Width : TILE_W;
    t->h = bmh->bmh_Height < TILE_H ? bmh->bmh_Height : TILE_H;
  }
}

/* --- drawing --------------------------------------------------------------- */

static void text_at(struct RastPort *rp, int x, int y, int maxw, const char *s, int pen)
{
  struct TextExtent te;
  int n = (int)strlen(s);
  if (maxw <= 0) return;
  n = TextFit(rp, (STRPTR)s, n, &te, NULL, 1, maxw, 32767);
  SetAPen(rp, pen);
  Move(rp, x, y + rp->TxBaseline);
  Text(rp, (STRPTR)s, n);
}

/* the status line of a game into out, its pen as the result */
static int game_state(HubGame *g, char *out, int outlen, int with_version)
{
  HubInstalled *in = installed(g);
  char v[32];
  latin(g->version, v, sizeof(v));
  if (in == NULL) {
    const char *what = g->local ? TX(T_SOON) : TX(T_AVAILABLE);
    if (with_version) snprintf(out, outlen, "%s  %s", v, what);
    else snprintf(out, outlen, "%s", what);
    return pens[P_DIM];
  }
  if (version_cmp(in->version, g->version) < 0) {
    if (with_version) snprintf(out, outlen, TX(T_NEW_SHORT), v);    /* tile */
    else snprintf(out, outlen, TX(T_NEW_VERSION), v, in->version);
    return pens[P_ACCENT];
  }
  snprintf(out, outlen, TX(T_INSTALLED), in->version);
  return pens[P_GREEN];
}

static void gallery_layout(struct IBox *box, int *x0, int *tile_h)
{
  int line = win != NULL ? win->RPort->TxHeight : 8;
  *tile_h = TILE_H + 2 * line + 14;
  cols = (box->Width - GAP) / (TILE_W + GAP);
  if (cols < 1) cols = 1;
  rows_visible = (box->Height - GAP) / (*tile_h + GAP);
  if (rows_visible < 1) rows_visible = 1;
  *x0 = box->Left + (box->Width - cols * (TILE_W + GAP) + GAP) / 2;
  if (*x0 < box->Left) *x0 = box->Left;
}

static void draw_gallery(struct RastPort *rp, struct IBox *box)
{
  int x0, tile_h, i, v, line = rp->TxHeight;
  char name[96], state[96];
  SetAPen(rp, pens[P_BG]);
  RectFill(rp, box->Left, box->Top, box->Left + box->Width - 1, box->Top + box->Height - 1);
  if (cat == NULL) {
    text_at(rp, box->Left + GAP, box->Top + GAP, box->Width - 2 * GAP, TX(T_NO_CATALOG),
            pens[P_DIM]);
    return;
  }
  gallery_layout(box, &x0, &tile_h);
  if (nview == 0) {
    text_at(rp, box->Left + GAP, box->Top + GAP, box->Width - 2 * GAP, TX(T_EMPTY), pens[P_DIM]);
    return;
  }
  for (v = 0; v < nview; v++) {
    HubGame *g = &cat->games[i = view[v]];
    int row = v / cols - top_row, col = v % cols;
    int x = x0 + col * (TILE_W + GAP), y = box->Top + GAP + row * (tile_h + GAP);
    int pen, maxw = TILE_W;
    if (row < 0 || y + tile_h > box->Top + box->Height) continue;
    if (x + TILE_W > box->Left + box->Width) maxw = box->Left + box->Width - x;
    if (maxw <= 0) continue;
    SetAPen(rp, pens[i == selected ? P_SEL : P_TILE]);
    RectFill(rp, x - 3, y - 3, x + maxw + 2, y + tile_h + 2);
    SetAPen(rp, pens[P_TILE]);
    RectFill(rp, x, y, x + maxw - 1, y + tile_h - 1);
    if (tiles != NULL && tiles[i].bm != NULL) {
      int w = tiles[i].w < maxw ? tiles[i].w : maxw;
      BltBitMapRastPort(tiles[i].bm, 0, 0, rp, x + (TILE_W - tiles[i].w) / 2 * (w == tiles[i].w),
                        y, w, tiles[i].h, 0xC0);
    }
    latin(hub_text(g->name, lang), name, sizeof(name));
    SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
    text_at(rp, x + 6, y + TILE_H + 4, maxw - 12, name, pens[P_TEXT]);
    SetSoftStyle(rp, 0, AskSoftStyle(rp));
    pen = game_state(g, state, sizeof(state), 1);
    text_at(rp, x + 6, y + TILE_H + 6 + line, maxw - 12, state, pen);
  }
}

/* lines of text wrapped to width: calls out(line, len) for each */
static void wrap(struct RastPort *rp, const char *s, int width,
                 void (*out)(void *u, const char *line, int len), void *u)
{
  while (*s) {
    struct TextExtent te;
    int n = (int)strlen(s), fit;
    const char *nl = strchr(s, '\n');
    if (nl != NULL) n = (int)(nl - s);
    fit = n > 0 ? (int)TextFit(rp, (STRPTR)s, n, &te, NULL, 1, width > 8 ? width : 8, 32767) : 0;
    if (fit < n) {
      int cut = fit;
      while (cut > 0 && s[cut] != ' ') cut--;
      if (cut > 0) fit = cut;
      if (fit == 0) fit = 1;
    }
    out(u, s, fit);
    s += fit;
    while (*s == ' ') s++;
    if (fit == n && *s == '\n') s++;
  }
}

typedef struct PageDraw {
  struct RastPort *rp;
  struct IBox *box;
  int y, pen;
} PageDraw;

static void page_line(void *u, const char *line, int len)
{
  PageDraw *p = (PageDraw *)u;
  char buf[256];
  if (p->y + p->rp->TxHeight > p->box->Top + p->box->Height) return;
  if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
  memcpy(buf, line, len);
  buf[len] = 0;
  text_at(p->rp, p->box->Left + 8, p->y, p->box->Width - 16, buf, p->pen);
  p->y += p->rp->TxHeight + 1;
}

static void draw_page(struct RastPort *rp, struct IBox *box)
{
  PageDraw p;
  HubGame *g;
  char a[600], b[96], c[64];
  SetAPen(rp, pens[P_TILE]);
  RectFill(rp, box->Left, box->Top, box->Left + box->Width - 1, box->Top + box->Height - 1);
  if (cat == NULL || selected < 0 || selected >= cat->ngames) return;
  g = &cat->games[selected];
  p.rp = rp;
  p.box = box;
  p.y = box->Top + 6;
  latin(hub_text(g->name, lang), b, sizeof(b));
  latin(g->version, c, sizeof(c));
  snprintf(a, sizeof(a), "%s  %s", b, c);
  SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
  p.pen = pens[P_TEXT];
  page_line(&p, a, (int)strlen(a));
  SetSoftStyle(rp, 0, AskSoftStyle(rp));
  p.pen = game_state(g, a, sizeof(a), 0);
  if (installed(g) != NULL) {
    int n = (int)strlen(a);
    snprintf(a + n, sizeof(a) - n, "  %s %s", TX(T_IN), installed(g)->dir);
  }
  page_line(&p, a, (int)strlen(a));
  p.y += 3;
  latin(hub_text(g->desc, lang), a, sizeof(a));
  p.pen = pens[P_TEXT];
  wrap(rp, a, box->Width - 16, page_line, &p);
  size_text(g->size, c, sizeof(c));
  snprintf(a, sizeof(a), TX(T_SIZE), c, (long)g->nfiles);
  {
    int n = (int)strlen(a);
    snprintf(a + n, sizeof(a) - n, "  -  github.com/%s", g->repo);
  }
  p.pen = pens[P_DIM];
  page_line(&p, a, (int)strlen(a));
  if (g->local) {
    p.pen = pens[P_DIM];
    wrap(rp, TX(T_NOT_PUBLIC), box->Width - 16, page_line, &p);
  }
}

static void draw_req(struct RastPort *rp, struct IBox *box)
{
  PageDraw p;
  HubGame *g;
  char a[300];
  int i, l;
  SetAPen(rp, pens[P_TILE]);
  RectFill(rp, box->Left, box->Top, box->Left + box->Width - 1, box->Top + box->Height - 1);
  if (cat == NULL || selected < 0 || selected >= cat->ngames) return;
  g = &cat->games[selected];
  p.rp = rp;
  p.box = box;
  p.y = box->Top + 6;
  SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
  p.pen = pens[P_TEXT];
  page_line(&p, TX(T_REQ), (int)strlen(TX(T_REQ)));
  SetSoftStyle(rp, 0, AskSoftStyle(rp));
  p.y += 2;
  l = g->nreq[lang] > 0 ? lang : HUB_EN;
  for (i = 0; i < g->nreq[l]; i++) {
    const char *r = g->req[l][i];
    /* the data line (Data: / Dane: / Daten: / ROMs) in the accent colour */
    p.pen = (strchr(r, ':') != NULL) ? pens[P_ACCENT] : pens[P_DIM];
    latin(r, a, sizeof(a));
    wrap(rp, a, box->Width - 16, page_line, &p);
  }
  if (g->nreq[l] == 0 && g->readmes > 0) {
    p.pen = pens[P_ACCENT];
    wrap(rp, TX(T_NEEDS_DATA), box->Width - 16, page_line, &p);
  }
}

static ULONG render_hook(struct Hook *h, Object *o, struct gpRender *msg)
{
  struct IBox *box = NULL;
  (void)h;
  GetAttr(SPACE_AreaBox, o, (ULONG *)&box);
  if (box == NULL || msg->gpr_RPort == NULL) return 0;
  if (o == g_gallery) draw_gallery(msg->gpr_RPort, box);
  else if (o == g_req) draw_req(msg->gpr_RPort, box);
  else draw_page(msg->gpr_RPort, box);
  return 0;
}

/* --- the history of releases (list) --------------------------------------- */

static void notes_add(void *u, const char *line, int len)
{
  char buf[256];
  struct Node *n;
  (void)u;
  if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
  memcpy(buf, line, len);
  buf[len] = 0;
  n = AllocListBrowserNode(1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)buf, TAG_DONE);
  if (n != NULL) AddTail(&notes_list, n);
}

/* a line that starts its own line in the history: empty, a point ("- ",
 * "* "), an underline ("===", "---"), a version ("0.139 (18.11.2026...)") */
static int own_line(const char *l)
{
  while (*l == ' ') l++;
  if (*l == 0 || *l == '\n') return 1;
  if ((l[0] == '-' || l[0] == '*' || l[0] == '+') && (l[1] == ' ' || l[1] == '-' || l[1] == '\n')) {
    return 1;
  }
  if (l[0] == '=' || (l[0] == '-' && l[1] == '-')) return 1;
  if (l[0] >= '0' && l[0] <= '9') return 1;
  return 0;
}

/* the lines of a paragraph, broken in the file at ~70 characters, joined
 * into one, so that the list wraps them to its own width (in place) */
static void reflow(char *t)
{
  char *r = t, *w = t;
  while (*r) {
    char *nl = strchr(r, '\n');
    int len = nl ? (int)(nl - r) : (int)strlen(r);
    int join = nl != NULL && len > 0 && !own_line(nl + 1) &&
               !(r[0] == '=' || (r[0] == '-' && r[1] == '-'));
    memmove(w, r, len);
    w += len;
    if (nl == NULL) break;
    r = nl + 1;
    if (join) {
      while (*r == ' ') r++;
      *w++ = ' ';
    } else {
      *w++ = '\n';
    }
  }
  *w = 0;
}

static void fill_notes(void)
{
  char name[300];
  char *text = NULL, *conv;
  struct IBox *box = NULL;
  int width = 400;
  if (win == NULL) return;
  SetGadgetAttrs((struct Gadget *)g_notes, win, NULL, LISTBROWSER_Labels, ~0UL, TAG_DONE);
  FreeListBrowserList(&notes_list);
  NewList(&notes_list);
  if (cat != NULL && selected >= 0) {
    HubGame *g = &cat->games[selected];
    int l = lang;
    for (;;) {
      if (g->notes[l] != NULL) {
        hub_cache_notes(&work, g->key, l, name, sizeof(name));
        text = hub_read_file(name, 1UL << 20, NULL);
      }
      if (text != NULL || l == HUB_EN) break;
      l = HUB_EN;
    }
  }
  GetAttr(GA_Width, g_notes, (ULONG *)&width);
  (void)box;
  if (text != NULL) {
    ULONG n = strlen(text) + 1;
    conv = (char *)hub_malloc(n);
    if (conv != NULL) {
      char *s;
      hub_to_latin(text, conv, (int)n);
      for (s = conv; *s; s++) if (*s == '\t') *s = ' ';
      reflow(conv);
      wrap(win->RPort, conv, width - 26, notes_add, NULL);   /* the scroller and the margins */
      hub_free(conv);
    }
    hub_free(text);
  } else if (cat != NULL && selected >= 0) {
    notes_add(NULL, TX(T_HISTORY_NONE), (int)strlen(TX(T_HISTORY_NONE)));
  }
  SetGadgetAttrs((struct Gadget *)g_notes, win, NULL, LISTBROWSER_Labels, (ULONG)&notes_list,
                 LISTBROWSER_Top, 0, TAG_DONE);
}

/* --- buttons and texts ---------------------------------------------------- */

static void update_buttons(void)
{
  /* two buttons: Install for a game that is not there, Update for an installed
   * one with a newer version in the catalog */
  int none = cat == NULL || selected < 0;
  int can_install = 0, can_update = 0;
  if (!none && !work.busy && !cat->games[selected].local) {
    HubGame *g = &cat->games[selected];
    HubInstalled *in = installed(g);
    if (in == NULL) can_install = 1;
    else if (version_cmp(in->version, g->version) < 0) can_update = 1;
  }
  if (win == NULL) return;
  SetGadgetAttrs((struct Gadget *)g_guide, win, NULL, GA_Text, (ULONG)TX(T_GUIDE),
                 GA_Disabled, none, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_install, win, NULL, GA_Text, (ULONG)TX(T_INSTALL),
                 GA_Disabled, !can_install, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_update, win, NULL, GA_Text, (ULONG)TX(T_UPDATE),
                 GA_Disabled, !can_update, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_stop, win, NULL, GA_Text, (ULONG)TX(T_STOP),
                 GA_Disabled, !work.busy, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_refresh, win, NULL, GA_Text, (ULONG)TX(T_REFRESH),
                 GA_Disabled, work.busy, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_gamesdir, win, NULL, GA_Text, (ULONG)TX(T_GAMESDIR),
                 GA_Disabled, work.busy, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_appsdir, win, NULL, GA_Text, (ULONG)TX(T_APPSDIR),
                 GA_Disabled, work.busy, TAG_DONE);
}

static void update_scroller(void)
{
  int total = (nview + cols - 1) / cols;
  if (top_row > total - rows_visible) top_row = total - rows_visible;
  if (top_row < 0) top_row = 0;
  if (win != NULL) {
    SetGadgetAttrs((struct Gadget *)g_scroll, win, NULL, SCROLLER_Total, total,
                   SCROLLER_Visible, rows_visible, SCROLLER_Top, top_row, TAG_DONE);
  }
}

/* cols / rows_visible for the gallery as it is now on the screen */
static void update_layout(void)
{
  struct IBox *box = NULL;
  int x0, th;
  GetAttr(SPACE_AreaBox, g_gallery, (ULONG *)&box);
  if (box != NULL && box->Width > 0 && box->Height > 0) gallery_layout(box, &x0, &th);
}

static void refresh_all(void)
{
  update_layout();
  update_scroller();
  redraw(g_gallery);
  redraw(g_page);
  redraw(g_req);
  update_buttons();
  fill_notes();
}

static void select_game(int i)
{
  if (cat == NULL || i < 0 || i >= cat->ngames) return;
  selected = i;
  redraw(g_gallery);
  redraw(g_page);
  redraw(g_req);
  update_buttons();
  fill_notes();
}

static void ready_status(const char *extra)
{
  set_status(extra != NULL ? extra : "");
}

/* --- the filter ------------------------------------------------------------- */

static int view_pos(int game)
{
  int v;
  for (v = 0; v < nview; v++) {
    if (view[v] == game) return v;
  }
  return -1;
}

/* the games of the chosen group; the selection stays when it is in it */
static void apply_filter(void)
{
  int i;
  nview = 0;
  for (i = 0; cat != NULL && i < cat->ngames && nview < 128; i++) {
    if (filter == 0 || hub_game_has_tag(&cat->games[i], filter_tag[filter])) view[nview++] = i;
  }
  if (view_pos(selected) < 0) selected = nview > 0 ? view[0] : -1;
  top_row = 0;
}

static void set_filter_buttons(void)
{
  int i;
  if (win == NULL) return;
  for (i = 0; i < FILTERS; i++) {
    SetGadgetAttrs((struct Gadget *)g_filter[i], win, NULL,
                   GA_Text, (ULONG)filter_label[lang == HUB_PL ? 1 : lang == HUB_DE ? 2 : 0][i],
                   GA_Selected, i == filter, TAG_DONE);
  }
}

/* --- the catalog ------------------------------------------------------------ */

static void take_catalog(HubCatalog *c)
{
  char key[32] = "";
  int i;
  if (cat != NULL && selected >= 0) strncpy(key, cat->games[selected].key, sizeof(key) - 1);
  free_tiles();
  hub_catalog_free(cat);
  cat = c;
  selected = cat != NULL && cat->ngames > 0 ? 0 : -1;
  for (i = 0; cat != NULL && i < cat->ngames; i++) {
    if (strcmp(cat->games[i].key, key) == 0) selected = i;
  }
  load_tiles();
  apply_filter();
}

static void load_cached_catalog(void)
{
  char name[300], err[160];
  char *text;
  snprintf(name, sizeof(name), "%s/catalog.txt", CACHE_DIR);
  text = hub_read_file(name, 4UL << 20, NULL);
  if (text != NULL) {
    HubCatalog *c = hub_catalog_parse(text, err, sizeof(err));
    if (c != NULL) take_catalog(c);
  }
}

static void start_refresh(void)
{
  if (work.busy) return;
  strncpy(work.catalog_src, prefs.catalog[0] ? prefs.catalog : CATALOG_URL,
          sizeof(work.catalog_src) - 1);
  work.job = HUB_JOB_REFRESH;
  set_status(TX(T_LOADING));
  hub_worker_go(&work);
  update_buttons();
}

/* --- install --------------------------------------------------------------- */

static int ask(const char *body, const char *buttons)
{
  struct EasyStruct es;
  es.es_StructSize = sizeof(es);
  es.es_Flags = 0;
  es.es_Title = (UBYTE *)"WARP HUB";
  es.es_TextFormat = (UBYTE *)"%s";
  es.es_GadgetFormat = (UBYTE *)buttons;
  return EasyRequest(win, &es, NULL, (ULONG)body);
}

static int ask_drawer(const char *title, const char *start, char *out, int outlen)
{
  struct FileRequester *fr;
  int ok = 0;
  fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
                                                   ASLFR_Window, (ULONG)win,
                                                   ASLFR_TitleText, (ULONG)title,
                                                   ASLFR_DrawersOnly, TRUE,
                                                   ASLFR_InitialDrawer,
                                                   (ULONG)(start != NULL && start[0] ? start : "Work:"),
                                                   ASLFR_SleepWindow, TRUE,
                                                   TAG_DONE);
  if (fr == NULL) return 0;
  if (AslRequest(fr, NULL) && fr->fr_Drawer != NULL && fr->fr_Drawer[0]) {
    strncpy(out, (char *)fr->fr_Drawer, outlen - 1);
    out[outlen - 1] = 0;
    ok = 1;
  }
  FreeAslRequest(fr);
  return ok;
}

static int choose_dir(HubGame *g, char *out, int outlen)
{
  char title[160], name[96];
  latin(hub_text(g->name, lang), name, sizeof(name));
  snprintf(title, sizeof(title), TX(T_CHOOSE_DIR), name);
  /* programs (group "apps") go to the programs drawer, games to the games drawer */
  return ask_drawer(title, hub_game_has_tag(g, "apps") && prefs.appsdir[0] ? prefs.appsdir
                                                                         : prefs.gamesdir,
                    out, outlen);
}

/* the version in "$VER: <name> <version>" of a program, "" when none.
 * Read in 32 KB pieces (the last 64 bytes of a piece are kept, so a $VER
 * across two pieces is found too); stops at the first $VER. */
#define VER_PIECE 32768
#define VER_KEEP 64
static void program_version(const char *file, char *out, int outlen)
{
  BPTR fh = Open((CONST_STRPTR)file, MODE_OLDFILE);
  char *buf;
  LONG have = 0, got;
  out[0] = 0;
  if (fh == 0) return;
  buf = (char *)hub_malloc(VER_PIECE + VER_KEEP + 1);
  if (buf == NULL) {
    Close(fh);
    return;
  }
  while ((got = Read(fh, buf + have, VER_PIECE)) > 0) {
    LONG len = have + got, i;
    for (i = 0; i + 6 <= len; i++) {
      if (buf[i] == '$' && strncmp(buf + i, "$VER: ", 6) == 0) break;
    }
    if (i + 6 <= len) {
      const char *p, *end;
      /* the whole line of $VER in the buffer */
      if (len - i < 100) {
        LONG more = Read(fh, buf + len, VER_PIECE + VER_KEEP - len);
        if (more > 0) len += more;
      }
      p = buf + i + 6;
      end = buf + len;
      /* the first word that starts with a digit */
      while (p < end && *p && *p != '\n') {
        while (p < end && *p == ' ') p++;
        if (p < end && *p >= '0' && *p <= '9') {
          int n = 0;
          while (p < end && n < outlen - 1 && ((*p >= '0' && *p <= '9') || *p == '.')) out[n++] = *p++;
          out[n] = 0;
          break;
        }
        while (p < end && *p && *p != ' ' && *p != '\n') p++;
      }
      break;
    }
    have = len < VER_KEEP ? len : VER_KEEP;
    memmove(buf, buf + len - have, have);
  }
  hub_free(buf);
  Close(fh);
}

/* the games of the catalog found in the games drawer: <dir>/<Game>/<program>.
 * A game has one or more programs (a file of the drawer whose icon is a
 * player's file); the version is the $VER of the largest program that has
 * one (OpenDUNE, not its MakeMusic; devilutionx, not the Hellfire starter).
 * Remembered in WarpHub.prefs. */
static int scan_games_dir(const char *dir, int save)
{
  int i, j, found = 0;
  BPTR dlock = Lock((CONST_STRPTR)dir, ACCESS_READ);
  if (dlock == 0) return -1;      /* not there now: keep what we know */
  UnLock(dlock);
  for (i = 0; cat != NULL && i < cat->ngames; i++) {
    HubGame *g = &cat->games[i];
    char best[24] = "";
    ULONG best_size = 0;
    int here = 0;
    for (j = 0; j < g->nfiles; j++) {
      const char *path = g->files[j].path;
      char name[300], ver[24];
      const char *slash = strchr(path, '/');
      int n = (int)strlen(path);
      BPTR lock;
      if (slash == NULL || strchr(slash + 1, '/') != NULL ||
          (n > 5 && strcmp(path + n - 5, ".info") == 0)) {
        continue;
      }
      snprintf(name, sizeof(name), "%s.info", path);
      {
        HubFile *icon = hub_game_file(g, name);
        if (icon == NULL || icon->flag != 'K') continue;
      }
      strncpy(name, dir, sizeof(name) - 1);
      name[sizeof(name) - 1] = 0;
      if (!AddPart((STRPTR)name, (CONST_STRPTR)path, sizeof(name))) continue;
      lock = Lock((CONST_STRPTR)name, ACCESS_READ);
      if (lock == 0) continue;
      UnLock(lock);
      here = 1;
      program_version(name, ver, sizeof(ver));
      if (ver[0] && g->files[j].size >= best_size) {
        strcpy(best, ver);
        best_size = g->files[j].size;
      }
    }
    if (here) {
      hub_prefs_set_installed(&prefs, g->key, best[0] ? best : "0", dir);
      found++;
    } else {
      HubInstalled *in = hub_prefs_installed(&prefs, g->key);
      if (in != NULL && Stricmp((CONST_STRPTR)in->dir, (CONST_STRPTR)dir) == 0) {
        hub_prefs_remove_installed(&prefs, g->key);   /* deleted or moved away */
      }
    }
  }
  if (save) hub_prefs_save(&prefs, PREFS_FILE);
  return found;
}

/* at every start: the games drawer and every drawer a game was installed to,
 * looked at again - the player may have added, removed or updated a game
 * without WARP HUB. Needs the catalog (cached one, or the first download). */
static int games_scanned;

static void rescan_games(void)
{
  char dirs[HUB_MAX_INSTALLED + 2][256];
  int ndirs = 0, i, k;
  if (cat == NULL || games_scanned) return;
  games_scanned = 1;
  for (i = -2; i < prefs.ninst; i++) {
    const char *d = i == -2 ? prefs.gamesdir : i == -1 ? prefs.appsdir : prefs.inst[i].dir;
    for (k = 0; k < ndirs; k++) {
      if (Stricmp((CONST_STRPTR)dirs[k], (CONST_STRPTR)d) == 0) break;
    }
    if (k == ndirs && d[0] && ndirs < HUB_MAX_INSTALLED + 2) {
      strncpy(dirs[ndirs], d, 255);
      dirs[ndirs++][255] = 0;
    }
  }
  if (ndirs == 0) return;
  set_status(TX(T_SCANNING));
  for (k = 0; k < ndirs; k++) scan_games_dir(dirs[k], 0);
  hub_prefs_save(&prefs, PREFS_FILE);
  set_status("");
}

/* the games drawer (apps = 0) or the programs drawer (apps = 1); both are
 * searched for everything of the catalog */
/* --- the "how to start" window ---------------------------------------------- */

static struct List guide_list;

static void guide_add(void *u, const char *line, int len)
{
  char buf[256];
  struct Node *n;
  if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
  memcpy(buf, line, len);
  buf[len] = 0;
  n = AllocListBrowserNode(1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)buf, TAG_DONE);
  if (n != NULL) AddTail((struct List *)u, n);
}

static void guide_text(HubGame *g, int width)
{
  HubFile *gf[HUB_MAX_README];
  char file[300], a[300];
  int k, ng = hub_guide_files(g, gf, HUB_MAX_README);
  if (ng == 0) wrap(&scr->RastPort, TX(T_GUIDE_NONE), width, guide_add, &guide_list);
  for (k = 0; k < ng; k++) {
    ULONG len = 0;
    char *text, *conv, *s, *d;
    hub_cache_guide(&work, g->key, k, file, sizeof(file));
    text = hub_read_file(file, 256UL << 10, &len);
    if (text == NULL) {
      wrap(&scr->RastPort, TX(T_GUIDE_MISSING), width, guide_add, &guide_list);
      break;
    }
    if (strstr(gf[k]->path, "/doc/Install.txt") == NULL) {
      /* a ReadMe.txt of an empty drawer: say which drawer */
      char dir[200];
      char *slash;
      strncpy(dir, gf[k]->path, sizeof(dir) - 1);
      dir[sizeof(dir) - 1] = 0;
      slash = strrchr(dir, '/');
      if (slash != NULL) *slash = 0;
      if (k > 0) guide_add(&guide_list, "", 0);
      snprintf(a, sizeof(a), TX(T_PUT_IN), dir);
      guide_add(&guide_list, a, (int)strlen(a));
      guide_add(&guide_list, "", 0);
    }
    conv = (char *)hub_malloc(len + 1);
    if (conv != NULL) {
      hub_to_latin(text, conv, (int)len + 1);
      for (s = d = conv; *s; s++) {          /* no CR, tabs as spaces */
        if (*s == '\r') continue;
        *d++ = *s == '\t' ? ' ' : *s;
      }
      *d = 0;
      wrap(&scr->RastPort, conv, width, guide_add, &guide_list);
      hub_free(conv);
    }
    hub_free(text);
  }
}

/* a window of its own over the main one; the main one waits (busy pointer)
 * and what was clicked in it meanwhile is thrown away */
static void show_guide(void)
{
  HubGame *g;
  Object *gw, *lb;
  struct Window *w2;
  char title[200], name[96];
  ULONG sig = 0, result;
  UWORD code;
  int done = 0, ww, wh;
  if (cat == NULL || selected < 0 || win == NULL) return;
  g = &cat->games[selected];
  latin(hub_text(g->name, lang), name, sizeof(name));
  snprintf(title, sizeof(title), "%s - %s", name, TX(T_GUIDE));
  ww = win->Width - 40;
  wh = win->Height - 40;
  NewList(&guide_list);
  guide_text(g, ww - 50);   /* the list: margins and its scroller */
  gw = NewObject(WINDOW_GetClass(), NULL,
      WA_Title, (ULONG)title,
      WA_PubScreen, (ULONG)scr,
      WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
      WA_Activate, TRUE,
      WA_Left, win->LeftEdge + 20, WA_Top, win->TopEdge + 20,
      WA_Width, ww, WA_Height, wh,
      WA_IDCMP, IDCMP_RAWKEY,
      WINDOW_Layout, (ULONG)(NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)(lb = NewObject(LISTBROWSER_GetClass(), NULL,
          GA_ReadOnly, TRUE,
          LISTBROWSER_Labels, (ULONG)&guide_list,
          LISTBROWSER_ShowSelected, FALSE,
        TAG_END)),
        LAYOUT_AddChild, (ULONG)(NewObject(BUTTON_GetClass(), NULL,
          GA_ID, 1, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_CLOSE),
        TAG_END)),
        CHILD_WeightedHeight, 0,
      TAG_END)),
    TAG_END);
  if (gw != NULL) {
    SetAttrs(win_obj, WA_BusyPointer, TRUE, TAG_DONE);
    w2 = (struct Window *)RA_OpenWindow(gw);
    if (w2 != NULL) GetAttr(WINDOW_SigMask, gw, &sig);
    while (w2 != NULL && !done) {
      ULONG s = Wait(sig | SIGBREAKF_CTRL_C);
      if (s & SIGBREAKF_CTRL_C) {
        SetSignal(SIGBREAKF_CTRL_C, SIGBREAKF_CTRL_C);   /* for the main loop too */
        done = 1;
      }
      while ((result = RA_HandleInput(gw, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
          case WMHI_CLOSEWINDOW: done = 1; break;
          case WMHI_GADGETUP: if ((result & WMHI_GADGETMASK) == 1) done = 1; break;
          case WMHI_RAWKEY: {
            ULONG top = 0;
            LONG step = 0;
            if (code == 0x45) done = 1;                              /* Esc */
            else if (code == 0x7A || code == 0x4C) step = -3;        /* wheel / up */
            else if (code == 0x7B || code == 0x4D) step = 3;         /* wheel / down */
            if (step != 0) {
              GetAttr(LISTBROWSER_Top, lb, &top);
              if (step < 0 && top < (ULONG)-step) top = 0;
              else top += step;
              SetGadgetAttrs((struct Gadget *)lb, w2, NULL, LISTBROWSER_Top, top, TAG_DONE);
            }
            break;
          }
        }
      }
    }
    DisposeObject(gw);
    SetAttrs(win_obj, WA_BusyPointer, FALSE, TAG_DONE);
    while (RA_HandleInput(win_obj, &code) != WMHI_LASTMSG) {
    }
  }
  FreeListBrowserList(&guide_list);
}

static void choose_games_dir(int apps)
{
  char dir[256], a[400];
  char *keep = apps ? prefs.appsdir : prefs.gamesdir;
  int n;
  if (work.busy) return;
  if (!ask_drawer(TX(apps ? T_CHOOSE_APPSDIR : T_CHOOSE_GAMESDIR),
                  keep[0] ? keep : prefs.gamesdir, dir, sizeof(dir))) return;
  strncpy(keep, dir, 255);
  keep[255] = 0;
  n = scan_games_dir(dir, 1);
  if (n < 0) n = 0;
  snprintf(a, sizeof(a), TX(T_FOUND), n, dir);
  set_status(a);
  refresh_all();
}

static void start_install(void)
{
  HubGame *g;
  HubInstalled *in;
  char dir[256], body[700], name[96], v[32], sz[32];
  if (work.busy || cat == NULL || selected < 0) return;
  g = &cat->games[selected];
  in = installed(g);
  latin(hub_text(g->name, lang), name, sizeof(name));
  latin(g->version, v, sizeof(v));
  if (test_dir[0]) {
    strncpy(dir, in != NULL ? in->dir : test_dir, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
  } else if (in != NULL) {
    strncpy(dir, in->dir, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    snprintf(body, sizeof(body), TX(T_ASK_UPDATE), name, v, dir);
    if (ask(body, TX(T_YES_NO)) != 1) return;
  } else {
    if (!choose_dir(g, dir, sizeof(dir))) return;
    size_text(g->size, sz, sizeof(sz));
    snprintf(body, sizeof(body), TX(T_ASK_INSTALL), name, v, dir, sz);
    if (ask(body, TX(T_YES_NO)) != 1) return;
  }
  strncpy(work.key, g->key, sizeof(work.key) - 1);
  strncpy(work.dir, dir, sizeof(work.dir) - 1);
  strncpy(work.old_version, in != NULL ? in->version : "", sizeof(work.old_version) - 1);
  work.cat = cat;
  work.job = HUB_JOB_INSTALL;
  snprintf(body, sizeof(body), TX(T_CHECKING), name);
  set_status(body);
  hub_worker_go(&work);
  update_buttons();
}

/* --- the network process tells something ------------------------------------ */

static void worker_news(void)
{
  char a[400];
  if (work.busy) {
    if (work.total > 0) {
      long pct = (long)(((work.done >> 8) * 100UL) / ((work.total >> 8) + 1));
      snprintf(a, sizeof(a), TX(T_DOWNLOADING), work.file, pct);
      strncpy(status_text, a, sizeof(status_text) - 1);
      set_progress(work.done, work.total);
    }
    return;
  }
  if (work.finished == HUB_JOB_REFRESH) {
    work.finished = 0;
    if (work.new_cat != NULL) {
      take_catalog(work.new_cat);
      work.new_cat = NULL;
      rescan_games();           /* first start: no cached catalog before */
      ready_status(work.msg[0] ? work.msg : NULL);
    } else {
      snprintf(a, sizeof(a), TX(T_CACHE_ONLY), work.msg);
      if (cat == NULL) snprintf(a, sizeof(a), TX(T_NO_NET), work.msg);
      set_status(a);
      set_progress(0, 0);
    }
    refresh_all();
    if (test_key[0] && cat != NULL && hub_catalog_find(cat, test_key) != NULL) {
      selected = (int)(hub_catalog_find(cat, test_key) - cat->games);
      test_key[0] = 0;
      refresh_all();
      start_install();
    }
    return;
  } else if (work.finished == HUB_JOB_INSTALL) {
    HubGame *g = hub_catalog_find(cat, work.key);
    work.finished = 0;
    if (work.ok && g != NULL) {
      char name[96], v[32];
      int i, n;
      hub_prefs_set_installed(&prefs, g->key, g->version, work.dir);
      hub_prefs_save(&prefs, PREFS_FILE);
      latin(hub_text(g->name, lang), name, sizeof(name));
      latin(g->version, v, sizeof(v));
      n = snprintf(a, sizeof(a), TX(T_DONE), name, v, work.dir);
      if (work.plan.files == 0 && n < (int)sizeof(a)) {
        n += snprintf(a + n, sizeof(a) - n, " %s", TX(T_UP_TO_DATE));
      }
      set_status(a);
      set_progress(0, 0);
      if (g->readmes > 0 && work.plan.files > 0 && !test_dir[0]) {
        refresh_all();
        show_guide();           /* what to add and where */
      }
      (void)i;
    } else {
      snprintf(a, sizeof(a), TX(T_FAILED), work.msg);
      set_status(a);
      set_progress(0, 0);
      if (!test_dir[0]) ask(a, TX(T_OK));
    }
    refresh_all();
  }
  update_buttons();
}

/* --- the window ------------------------------------------------------------ */

static void get_pens(void)
{
  int i;
  for (i = 0; i < P_COUNT; i++) {
    ULONG c = pen_rgb[i];
    pens[i] = ObtainBestPen(scr->ViewPort.ColorMap, (c >> 16) * 0x01010101UL,
                            ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL,
                            OBP_Precision, PRECISION_GUI, TAG_DONE);
  }
}

static void free_pens(void)
{
  int i;
  for (i = 0; i < P_COUNT; i++) {
    if (pens[i] >= 0) ReleasePen(scr->ViewPort.ColorMap, pens[i]);
    pens[i] = -1;
  }
}

/* width of the widest label of the buttons in all languages */
static int button_width(void)
{
  static const int ids[] = {T_INSTALL, T_UPDATE, T_REPAIR, T_STOP, T_GAMESDIR, T_APPSDIR,
                            T_GUIDE};
  struct RastPort *rp = &scr->RastPort;
  int w = 0, l, i;
  for (l = 0; l < 3; l++) {
    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
      const char *t = texts[l][ids[i]];
      int n = TextLength(rp, (STRPTR)t, (int)strlen(t));
      if (n > w) w = n;
    }
  }
  return w + 24;
}

static Object *filter_row(void)
{
  Object *row = NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                          LAYOUT_InnerSpacing, 2, TAG_END);
  int i;
  if (row == NULL) return NULL;
  for (i = 0; i < FILTERS; i++) {
    g_filter[i] = NewObject(BUTTON_GetClass(), NULL,
                            GA_ID, GID_FILTER + i, GA_RelVerify, TRUE,
                            GA_Text, (ULONG)filter_label[lang == HUB_PL ? 1 : lang == HUB_DE ? 2 : 0][i],
                            BUTTON_PushButton, TRUE, GA_Selected, i == filter, TAG_END);
    if (g_filter[i] != NULL) SetAttrs(row, LAYOUT_AddChild, (ULONG)g_filter[i], TAG_END);
  }
  return row;
}

static Object *make_window(void)
{
  static const char *lang_names[] = {"English", "Polski", "Deutsch"};
  int i, bw = button_width();
  int small = TILE_W != 224, gallery_h;
  /* the window fits the screen: inner width for 3 tiles at most, the height
   * 85% of a big screen, nearly all of a small one */
  int win_w = 3 * (TILE_W + GAP) + GAP + 40;
  int win_h = small ? scr->Height - scr->BarHeight - 34 : scr->Height * 85 / 100 - scr->BarHeight - 30;
  if (win_w > scr->Width - 16) win_w = scr->Width - 16;
  if (win_h > 860) win_h = 860;
  /* the gallery is exactly whole rows of tiles high (no empty strip under
   * them): 1 row on a small screen, as many as fit in 55% of a big one */
  {
    int tile_h = TILE_H + 2 * scr->Font->ta_YSize + 14;
    int rows = small ? 1 : (win_h * 55 / 100 - GAP) / (tile_h + GAP);
    if (rows < 1) rows = 1;
    if (rows > 3) rows = 3;
    gallery_h = rows * (tile_h + GAP) + GAP + 4;      /* + the bevel */
  }
  for (i = 0; i < 3; i++) {
    struct Node *n = AllocChooserNode(CNA_Text, (ULONG)lang_names[i], TAG_DONE);
    if (n != NULL) AddTail(&lang_list, n);
  }
  gallery_hook.h_Entry = (HOOKFUNC)HookEntry;
  gallery_hook.h_SubEntry = (HOOKFUNC)render_hook;
  page_hook = gallery_hook;
  req_hook = gallery_hook;

  return NewObject(WINDOW_GetClass(), NULL,
      WA_Title, (ULONG)"WARP HUB",
      WA_ScreenTitle, (ULONG)"WARP HUB " WARPHUB_VERSION,
      WA_PubScreen, (ULONG)scr,
      WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
      WA_Activate, TRUE,
      WA_InnerWidth, win_w,
      /* as low as the layout allows: the history of releases then ends with
       * the last button (the user wants them the same height) */
      WA_InnerHeight, 50,
      WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY,
      WINDOW_Position, WPOS_CENTERSCREEN,
      WINDOW_IconifyGadget, TRUE,
      WINDOW_IconTitle, (ULONG)"WARP HUB",
      WINDOW_AppPort, (ULONG)app_port,
      WINDOW_Layout, (ULONG)(NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)(filter_row()),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)(NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
          LAYOUT_AddChild, (ULONG)(g_gallery = NewObject(SPACE_GetClass(), NULL,
            GA_ID, GID_GALLERY, GA_ReadOnly, TRUE,
            SPACE_MinWidth, TILE_W + 2 * GAP,
            SPACE_MinHeight, gallery_h,
            SPACE_RenderHook, (ULONG)&gallery_hook,
            SPACE_BevelStyle, BVS_FIELD,
          TAG_END)),
          LAYOUT_AddChild, (ULONG)(g_scroll = NewObject(SCROLLER_GetClass(), NULL,
            GA_ID, GID_SCROLL, GA_RelVerify, TRUE,
            SCROLLER_Orientation, SORIENT_VERT,
            SCROLLER_Total, 1, SCROLLER_Visible, 1, SCROLLER_Top, 0,
          TAG_END)),
          CHILD_WeightedWidth, 0,
        TAG_END)),
        CHILD_WeightedHeight, 0,
        /* under the gallery: the page of the game and its history of releases
         * one under the other, equally wide; the buttons in a column on the right */
        LAYOUT_AddChild, (ULONG)(NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
          LAYOUT_AddChild, (ULONG)(NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_AddChild, (ULONG)(g_page = NewObject(SPACE_GetClass(), NULL,
              GA_ID, GID_PAGE, GA_ReadOnly, TRUE,
              SPACE_MinWidth, 200,
              SPACE_MinHeight, (scr->Font->ta_YSize + 1) * (small ? 7 : 8) + 12,
              SPACE_RenderHook, (ULONG)&page_hook,
              SPACE_BevelStyle, BVS_FIELD,
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_notes = NewObject(LISTBROWSER_GetClass(), NULL,
              GA_ID, GID_NOTES, GA_ReadOnly, TRUE,
              LISTBROWSER_Labels, (ULONG)&notes_list,
              LISTBROWSER_ShowSelected, FALSE,
            TAG_END)),
            CHILD_WeightedHeight, 100,
          TAG_END)),
          LAYOUT_AddChild, (ULONG)(NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_AddChild, (ULONG)(g_req = NewObject(SPACE_GetClass(), NULL,
              GA_ID, GID_REQ, GA_ReadOnly, TRUE,
              SPACE_MinWidth, small ? 150 : 190,
              SPACE_MinHeight, (scr->Font->ta_YSize + 1) * (small ? 7 : 8) + 12,
              SPACE_RenderHook, (ULONG)&req_hook,
              SPACE_BevelStyle, BVS_FIELD,
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_guide = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_GUIDE, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_GUIDE),
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_install = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_INSTALL, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_INSTALL),
            TAG_END)),
            CHILD_WeightedHeight, 0,
            CHILD_MinWidth, bw,
            LAYOUT_AddChild, (ULONG)(g_update = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_UPDATE, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_UPDATE),
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_stop = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_STOP, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_STOP),
              GA_Disabled, TRUE,
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_gamesdir = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_GAMESDIR, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_GAMESDIR),
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_appsdir = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_APPSDIR, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_APPSDIR),
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_refresh = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_REFRESH, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_REFRESH),
            TAG_END)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)(g_lang = NewObject(CHOOSER_GetClass(), NULL,
              GA_ID, GID_LANG, GA_RelVerify, TRUE,
              CHOOSER_Labels, (ULONG)&lang_list,
              CHOOSER_Selected, lang,
            TAG_END)),
            CHILD_WeightedHeight, 0,
          TAG_END)),
          CHILD_WeightedWidth, 0,
        TAG_END)),
        CHILD_WeightedHeight, 100,
      TAG_END)),
    TAG_END);
}

static void click(int mx, int my)
{
  struct IBox *box = NULL;
  int x0, tile_h, col, row, i;
  GetAttr(SPACE_AreaBox, g_gallery, (ULONG *)&box);
  if (box == NULL || cat == NULL) return;
  if (mx < box->Left || my < box->Top || mx >= box->Left + box->Width ||
      my >= box->Top + box->Height) {
    return;
  }
  gallery_layout(box, &x0, &tile_h);
  if (mx < x0) return;
  col = (mx - x0) / (TILE_W + GAP);
  row = (my - box->Top - GAP) / (tile_h + GAP);
  if (col >= cols || my < box->Top + GAP) return;
  if ((mx - x0) % (TILE_W + GAP) >= TILE_W) return;
  i = (row + top_row) * cols + col;
  if (i < nview) select_game(view[i]);
}

static void scroll_rows(int d)
{
  top_row += d;
  update_scroller();
  redraw(g_gallery);
}

/* errors at the start: to the Shell only (from an icon libnix would open a
 * console window for stdout) */
static void say_error(const char *what)
{
  if (_WBenchMsg == NULL) printf("WarpHub: %s\n", what);
}

static int open_classes(void)
{
  WindowBase = OpenLibrary((CONST_STRPTR)"window.class", 44);
  LayoutBase = OpenLibrary((CONST_STRPTR)"gadgets/layout.gadget", 44);
  ButtonBase = OpenLibrary((CONST_STRPTR)"gadgets/button.gadget", 44);
  ChooserBase = OpenLibrary((CONST_STRPTR)"gadgets/chooser.gadget", 44);
  ListBrowserBase = OpenLibrary((CONST_STRPTR)"gadgets/listbrowser.gadget", 44);
  SpaceBase = OpenLibrary((CONST_STRPTR)"gadgets/space.gadget", 44);
  ScrollerBase = OpenLibrary((CONST_STRPTR)"gadgets/scroller.gadget", 44);
  FuelGaugeBase = OpenLibrary((CONST_STRPTR)"gadgets/fuelgauge.gadget", 44);
  if (!WindowBase) say_error("no window.class");
  if (!LayoutBase) say_error("no layout.gadget");
  if (!ButtonBase) say_error("no button.gadget");
  if (!ChooserBase) say_error("no chooser.gadget");
  if (!ListBrowserBase) say_error("no listbrowser.gadget");
  if (!SpaceBase) say_error("no space.gadget");
  if (!ScrollerBase) say_error("no scroller.gadget");
  if (!FuelGaugeBase) say_error("no fuelgauge.gadget");
  return WindowBase && LayoutBase && ButtonBase && ChooserBase && ListBrowserBase &&
         SpaceBase && ScrollerBase && FuelGaugeBase;
}

static void close_classes(void)
{
  struct Library **l[] = {&WindowBase, &LayoutBase, &ButtonBase, &ChooserBase,
                          &ListBrowserBase, &SpaceBase, &ScrollerBase, &FuelGaugeBase};
  int i;
  for (i = 0; i < 8; i++) {
    if (*l[i] != NULL) CloseLibrary(*l[i]);
    *l[i] = NULL;
  }
}

/* options: Shell arguments or Tool Types of the icon */

static void read_options(void)
{
  if (_WBenchMsg != NULL) {
    struct DiskObject *dobj;
    BPTR old = CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);
    dobj = GetDiskObject((STRPTR)_WBenchMsg->sm_ArgList[0].wa_Name);
    if (dobj != NULL) {
      STRPTR v;
      if ((v = FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"CATALOG")) != NULL) {
        strncpy(prefs.catalog, (char *)v, sizeof(prefs.catalog) - 1);
      }
      if ((v = FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"LANG")) != NULL) {
        strncpy(prefs.language, (char *)v, sizeof(prefs.language) - 1);
      }
      FreeDiskObject(dobj);
    }
    CurrentDir(old);
  } else {
    LONG args[5] = {0, 0, 0, 0, 0};
    struct RDArgs *rd = ReadArgs((CONST_STRPTR)"CATALOG/K,LANG/K,TESTINSTALL/K,TESTTO/K,"
                                 "TESTGAMESDIR/K", args, NULL);
    if (rd != NULL) {
      if (args[0]) strncpy(prefs.catalog, (char *)args[0], sizeof(prefs.catalog) - 1);
      if (args[1]) strncpy(prefs.language, (char *)args[1], sizeof(prefs.language) - 1);
      if (args[2]) strncpy(test_key, (char *)args[2], sizeof(test_key) - 1);
      if (args[3]) strncpy(test_dir, (char *)args[3], sizeof(test_dir) - 1);
      if (args[4]) strncpy(test_gamesdir, (char *)args[4], sizeof(test_gamesdir) - 1);
      FreeArgs(rd);
    }
  }
}

static int locale_lang(void)
{
  struct Locale *loc;
  int l = HUB_EN;
  if (LocaleBase == NULL) return l;
  loc = OpenLocale(NULL);
  if (loc != NULL) {
    const char *n = (const char *)loc->loc_LanguageName;
    if (n != NULL && (n[0] == 'p' || n[0] == 'P') && (n[1] == 'o' || n[1] == 'O')) l = HUB_PL;
    if (n != NULL && (n[0] == 'd' || n[0] == 'D') && (n[1] == 'e' || n[1] == 'E')) l = HUB_DE;
    CloseLocale(loc);
  }
  return l;
}

int main(void)
{
  ULONG win_sig = 0, work_mask, sigs;
  int done = 0, i;

  for (i = 0; i < P_COUNT; i++) pens[i] = -1;
  NewList(&notes_list);
  NewList(&lang_list);
  hub_prefs_load(&prefs, PREFS_FILE);
  read_options();
  lang = prefs.language[0] ? hub_lang(prefs.language) : locale_lang();
  strcpy(work.cache, CACHE_DIR);

  if (!open_classes()) {
    close_classes();
    return RETURN_FAIL;
  }
  scr = LockPubScreen(NULL);
  app_port = CreateMsgPort();
  work_signal = AllocSignal(-1);
  if (scr == NULL || app_port == NULL || work_signal < 0) {
    say_error("no screen / port / signal");
    goto out;
  }
  if (scr->Width < 1024 || scr->Height < 700) {
    TILE_W = 144;
    TILE_H = 90;
  }
  get_pens();
  load_cached_catalog();
  win_obj = make_window();
  if (win_obj == NULL) {
    say_error("window object not made");
    goto out;
  }
  win = (struct Window *)RA_OpenWindow(win_obj);
  if (win == NULL) {
    say_error("window not opened");
    goto out;
  }
  SetGadgetAttrs((struct Gadget *)g_lang, win, NULL, CHOOSER_Selected, lang, TAG_DONE);
  ready_status(NULL);
  if (test_gamesdir[0]) {
    char a[400];
    int n;
    strncpy(prefs.gamesdir, test_gamesdir, sizeof(prefs.gamesdir) - 1);
    n = scan_games_dir(test_gamesdir, 1);
    if (n < 0) n = 0;
    games_scanned = 1;
    snprintf(a, sizeof(a), TX(T_FOUND), n, test_gamesdir);
    set_status(a);
  }
  rescan_games();
  refresh_all();

  work_mask = 1UL << work_signal;
  if (hub_worker_start(&work, work_signal)) start_refresh();

  while (!done) {
    GetAttr(WINDOW_SigMask, win_obj, &win_sig);
    sigs = Wait(win_sig | work_mask | SIGBREAKF_CTRL_C);
    if (sigs & SIGBREAKF_CTRL_C) done = 1;
    if (sigs & work_mask) worker_news();
    if (sigs & win_sig) {
      ULONG result;
      UWORD code;
      while ((result = RA_HandleInput(win_obj, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
          case WMHI_CLOSEWINDOW:
            done = 1;
            break;
          case WMHI_ICONIFY:
            RA_Iconify(win_obj);
            win = NULL;
            break;
          case WMHI_UNICONIFY:
            win = (struct Window *)RA_OpenWindow(win_obj);
            if (win == NULL) done = 1; else refresh_all();
            break;
          case WMHI_NEWSIZE:
            refresh_all();
            break;
          case WMHI_MOUSEBUTTONS:
            if (code == SELECTDOWN && win != NULL) click(win->MouseX, win->MouseY);
            break;
          case WMHI_RAWKEY:
            if (code == 0x7A || code == 0x4C) scroll_rows(-1);       /* wheel / up */
            else if (code == 0x7B || code == 0x4D) scroll_rows(1);   /* wheel / down */
            else if (code == 0x45) done = 1;                         /* Esc */
            else if (code == 0x5F) show_guide();                     /* Help */
            else if (code >= 0x01 && code <= 0x08) {                 /* 1..8: group */
              filter = code - 0x01;
              set_filter_buttons();
              apply_filter();
              refresh_all();
            }
            else if ((code == 0x4E || code == 0x4F) && nview > 0) {
              /* right / left: the next / previous game shown, kept in view */
              int i = view_pos(selected) + (code == 0x4E ? 1 : -1);
              update_layout();
              if (i < 0) i = 0;
              if (i >= nview) i = nview - 1;
              if (i / cols < top_row) top_row = i / cols;
              if (i / cols >= top_row + rows_visible) top_row = i / cols - rows_visible + 1;
              update_scroller();
              select_game(view[i]);
            }
            break;
          case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
              case GID_REFRESH: start_refresh(); break;
              case GID_GAMESDIR: choose_games_dir(0); break;
              case GID_GUIDE: show_guide(); break;
              case GID_APPSDIR: choose_games_dir(1); break;
              case GID_FILTER: case GID_FILTER + 1: case GID_FILTER + 2: case GID_FILTER + 3:
              case GID_FILTER + 4: case GID_FILTER + 5: case GID_FILTER + 6: case GID_FILTER + 7:
                filter = (int)(result & WMHI_GADGETMASK) - GID_FILTER;
                set_filter_buttons();
                apply_filter();
                refresh_all();
                break;
              case GID_INSTALL:
              case GID_UPDATE: start_install(); break;
              case GID_STOP: hub_worker_break(); break;
              case GID_SCROLL: {
                ULONG t = 0;
                GetAttr(SCROLLER_Top, g_scroll, &t);
                top_row = (int)t;
                redraw(g_gallery);
                break;
              }
              case GID_LANG: {
                ULONG l = 0;
                GetAttr(CHOOSER_Selected, g_lang, &l);
                lang = (int)l;
                strcpy(prefs.language, lang == HUB_PL ? "pl" : lang == HUB_DE ? "de" : "en");
                hub_prefs_save(&prefs, PREFS_FILE);
                set_filter_buttons();
                ready_status(NULL);
                refresh_all();
                break;
              }
            }
            break;
        }
      }
    }
  }

out:
  /* the language (and the rest) once more at the end: it is also saved at
   * every change, this keeps it even if that write did not go through */
  if (win_obj != NULL) {
    strcpy(prefs.language, lang == HUB_PL ? "pl" : lang == HUB_DE ? "de" : "en");
    hub_prefs_save(&prefs, PREFS_FILE);
  }
  hub_worker_quit(&work);
  if (win_obj != NULL) DisposeObject(win_obj);
  free_tiles();
  hub_catalog_free(cat);
  if (scr != NULL) {
    free_pens();
    UnlockPubScreen(NULL, scr);
  }
  FreeListBrowserList(&notes_list);
  {
    struct Node *n;
    while ((n = RemHead(&lang_list)) != NULL) FreeChooserNode(n);
  }
  if (work_signal >= 0) FreeSignal(work_signal);
  if (app_port != NULL) DeleteMsgPort(app_port);
  close_classes();
  return RETURN_OK;
}
