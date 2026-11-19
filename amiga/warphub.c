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
  "https://raw.githubusercontent.com/cizar-szymon/WarpHub/main/catalog/catalog.txt"
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
  T_NOT_PUBLIC, T_EMPTY, T_NEW_SHORT, T_COUNT
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
    "No games in this group yet.", "New: %s"
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
    "Na razie brak gier w tej grupie.", "Nowa wersja %s"
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
    "Test: das Repo dieses Spiels ist noch nicht ö" "ffentlich - Installation nach der Verö" "ffentlichung.",
    "Noch keine Spiele in dieser Gruppe.", "Neu: %s"
  }
};

static int lang = HUB_EN;

/* the filter buttons: the group (TAGS of the catalog) and its label */
#define FILTERS 8
static const char *const filter_tag[FILTERS] = {
  "", "new", "apps", "warpextreme", "arcade", "pc", "mac", "other"
};
static const char *const filter_label[3][FILTERS] = {
  {"All", "New", "Programs", "WarpExtreme", "Arcade", "PC port", "MAC port", "Other"},
  {"Wszystkie", "Nowosci", "Programy", "WarpExtreme", "Arcade", "PC port", "MAC port", "Inne"},
  {"Alle", "Neu", "Programme", "WarpExtreme", "Arcade", "PC-Port", "MAC-Port", "Andere"}
};
static int filter = 0;
static int view[128], nview;       /* the games shown (indices into the catalog) */
/* tests from the Shell: install without requesters after the first refresh */
static char test_key[32], test_dir[256];
#define TX(i) (texts[lang == HUB_PL ? 1 : lang == HUB_DE ? 2 : 0][i])

/* --- state --------------------------------------------------------------- */

enum { GID_LANG = 1, GID_REFRESH, GID_GALLERY, GID_SCROLL, GID_PAGE, GID_INSTALL, GID_STOP,
       GID_NOTES, GID_PROGRESS, GID_FILTER };     /* GID_FILTER + 0..FILTERS-1 */

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
    *g_stop, *g_notes, *g_progress, *g_filter[FILTERS];
static struct List notes_list, lang_list;
static struct MsgPort *app_port;
static struct Hook gallery_hook, page_hook;
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

static void set_status(const char *text)
{
  strncpy(status_text, text, sizeof(status_text) - 1);
  if (win != NULL) {
    SetGadgetAttrs((struct Gadget *)g_progress, win, NULL, GA_Text, (ULONG)status_text,
                   TAG_DONE);
  }
}

static void set_progress(ULONG done, ULONG total)
{
  ULONG level = total ? (ULONG)(((done >> 8) * 100UL) / ((total >> 8) + 1)) : 0;
  if (level > 100) level = 100;
  if (win != NULL) {
    SetGadgetAttrs((struct Gadget *)g_progress, win, NULL, FUELGAUGE_Level, level,
                   GA_Text, (ULONG)status_text, TAG_DONE);
  }
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
  if (g->readmes > 0) {
    p.pen = pens[P_ACCENT];
    wrap(rp, TX(T_NEEDS_DATA), box->Width - 16, page_line, &p);
  }
  if (g->local) {
    p.pen = pens[P_DIM];
    wrap(rp, TX(T_NOT_PUBLIC), box->Width - 16, page_line, &p);
  }
}

static ULONG render_hook(struct Hook *h, Object *o, struct gpRender *msg)
{
  struct IBox *box = NULL;
  (void)h;
  GetAttr(SPACE_AreaBox, o, (ULONG *)&box);
  if (box == NULL || msg->gpr_RPort == NULL) return 0;
  if (o == g_gallery) draw_gallery(msg->gpr_RPort, box); else draw_page(msg->gpr_RPort, box);
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
  const char *label = TX(T_INSTALL);
  int none = cat == NULL || selected < 0;
  if (!none) {
    HubGame *g = &cat->games[selected];
    HubInstalled *in = installed(g);
    if (in != NULL) label = version_cmp(in->version, g->version) < 0 ? TX(T_UPDATE) : TX(T_REPAIR);
  }
  if (win == NULL) return;
  SetGadgetAttrs((struct Gadget *)g_install, win, NULL, GA_Text, (ULONG)label,
                 GA_Disabled, none || work.busy || cat->games[selected].local, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_stop, win, NULL, GA_Text, (ULONG)TX(T_STOP),
                 GA_Disabled, !work.busy, TAG_DONE);
  SetGadgetAttrs((struct Gadget *)g_refresh, win, NULL, GA_Text, (ULONG)TX(T_REFRESH),
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
  update_buttons();
  fill_notes();
}

static void select_game(int i)
{
  if (cat == NULL || i < 0 || i >= cat->ngames) return;
  selected = i;
  redraw(g_gallery);
  redraw(g_page);
  update_buttons();
  fill_notes();
}

static void ready_status(const char *extra)
{
  char a[300];
  snprintf(a, sizeof(a), TX(T_READY), cat != NULL ? cat->ngames : 0, extra ? extra : "");
  set_status(a);
  set_progress(0, 0);
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

static int choose_dir(HubGame *g, char *out, int outlen)
{
  struct FileRequester *fr;
  char title[160], name[96];
  int ok = 0;
  latin(hub_text(g->name, lang), name, sizeof(name));
  snprintf(title, sizeof(title), TX(T_CHOOSE_DIR), name);
  fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
                                                   ASLFR_Window, (ULONG)win,
                                                   ASLFR_TitleText, (ULONG)title,
                                                   ASLFR_DrawersOnly, TRUE,
                                                   ASLFR_InitialDrawer, (ULONG)"Work:",
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
        n += snprintf(a + n, sizeof(a) - n, "\n\n%s", TX(T_ADD_YOURSELF));
        for (i = 0; i < g->readmes && n < (int)sizeof(a); i++) {
          n += snprintf(a + n, sizeof(a) - n, "\n%s/%s", work.dir, g->readme[i]);
        }
        ask(a, TX(T_OK));
      }
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
  static const int ids[] = {T_INSTALL, T_UPDATE, T_REPAIR, T_STOP};
  struct RastPort *rp = &scr->RastPort;
  int w = 0, l, i;
  for (l = 0; l < 3; l++) {
    for (i = 0; i < 4; i++) {
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

  return NewObject(WINDOW_GetClass(), NULL,
      WA_Title, (ULONG)"WARP HUB",
      WA_ScreenTitle, (ULONG)"WARP HUB " WARPHUB_VERSION,
      WA_PubScreen, (ULONG)scr,
      WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
      WA_Activate, TRUE,
      WA_InnerWidth, win_w,
      WA_InnerHeight, win_h,
      WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY,
      WINDOW_Position, WPOS_CENTERSCREEN,
      WINDOW_IconifyGadget, TRUE,
      WINDOW_IconTitle, (ULONG)"WARP HUB",
      WINDOW_AppPort, (ULONG)app_port,
      WINDOW_Layout, (ULONG)(NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE,
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
            LAYOUT_AddChild, (ULONG)(g_install = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_INSTALL, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_INSTALL),
            TAG_END)),
            CHILD_WeightedHeight, 0,
            CHILD_MinWidth, bw,
            LAYOUT_AddChild, (ULONG)(g_stop = NewObject(BUTTON_GetClass(), NULL,
              GA_ID, GID_STOP, GA_RelVerify, TRUE, GA_Text, (ULONG)TX(T_STOP),
              GA_Disabled, TRUE,
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
            LAYOUT_AddChild, (ULONG)(NewObject(SPACE_GetClass(), NULL, TAG_END)),
            CHILD_WeightedHeight, 100,
          TAG_END)),
          CHILD_WeightedWidth, 0,
        TAG_END)),
        CHILD_WeightedHeight, 100,
        LAYOUT_AddChild, (ULONG)(g_progress = NewObject(FUELGAUGE_GetClass(), NULL,
          GA_ID, GID_PROGRESS,
          FUELGAUGE_Min, 0, FUELGAUGE_Max, 100, FUELGAUGE_Level, 0,
          FUELGAUGE_Percent, FALSE, FUELGAUGE_Ticks, 0,
          GA_Text, (ULONG)status_text,
        TAG_END)),
        CHILD_WeightedHeight, 0,
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
    LONG args[4] = {0, 0, 0, 0};
    struct RDArgs *rd = ReadArgs((CONST_STRPTR)"CATALOG/K,LANG/K,TESTINSTALL/K,TESTTO/K",
                                 args, NULL);
    if (rd != NULL) {
      if (args[0]) strncpy(prefs.catalog, (char *)args[0], sizeof(prefs.catalog) - 1);
      if (args[1]) strncpy(prefs.language, (char *)args[1], sizeof(prefs.language) - 1);
      if (args[2]) strncpy(test_key, (char *)args[2], sizeof(test_key) - 1);
      if (args[3]) strncpy(test_dir, (char *)args[3], sizeof(test_dir) - 1);
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
              case GID_FILTER: case GID_FILTER + 1: case GID_FILTER + 2: case GID_FILTER + 3:
              case GID_FILTER + 4: case GID_FILTER + 5: case GID_FILTER + 6: case GID_FILTER + 7:
                filter = (int)(result & WMHI_GADGETMASK) - GID_FILTER;
                set_filter_buttons();
                apply_filter();
                refresh_all();
                break;
              case GID_INSTALL: start_install(); break;
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
