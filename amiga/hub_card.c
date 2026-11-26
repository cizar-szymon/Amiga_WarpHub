/* hub_card - which Warp card is in this Amiga.
 *
 * As WARP NetMonitor (csWarpAmigaSoftware/WarpNetMonitor, CsWarpMini.h):
 * the control unit of the card in the expansion list (0x1400/101), then
 * getDiagnostic() of cswarp.library - boardName and warpBoardType. Only read,
 * nothing on the card is changed. The structure holds the fields used here at
 * the offsets of libraries/cswarp.h (CSWarpDiagData), checked below; the rest
 * is room, the library writes only the fields it knows (so it is cleared). */

#include <exec/types.h>
#include <exec/memory.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/expansion.h>
#include <inline/macros.h>
#include <stddef.h>
#include <string.h>

#include "hub_card.h"

#define CSWARP_VID      0x1400
#define CSWARP_PID_CTRL 101

/* WarpBoardType.h */
#define WBT_WARP7060    9
#define WBT_WARP1260X   13

typedef struct {
  char boardName[32];                /* 0 */
  UWORD hwVer;                       /* 32 */
  UWORD fmwVer;                      /* 34 */
  UBYTE pad0[344 - 36];
  UBYTE warpBoardType;               /* 344 */
  UBYTE pad1;
  ULONG cpuClockTable[8];            /* 346, obsolete */
  ULONG wifiState;                   /* 378 */
  UBYTE room[1024 - 382];
} CardDiag;

#define CARD_CHECK(name, cond) typedef char name[(cond) ? 1 : -1]
CARD_CHECK(cardA, offsetof(CardDiag, fmwVer) == 34);
CARD_CHECK(cardB, offsetof(CardDiag, warpBoardType) == 344);
CARD_CHECK(cardC, offsetof(CardDiag, wifiState) == 378);

#define cswGetDiagnostic(base, d) LP1(0x2a, UBYTE, getDiagnostic, CardDiag *, d, a0, , base)

/* "Warp 7060", "WARP1260X"...: digits and letters only, upper case */
static int name_has(const char *name, const char *what)
{
  char s[40];
  int n = 0;
  for (; *name && n < (int)sizeof(s) - 1; name++) {
    char c = *name;
    if (c >= 'a' && c <= 'z') c -= 32;
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) s[n++] = c;
  }
  s[n] = 0;
  return strstr(s, what) != NULL;
}

void hub_card_read(HubCard *c)
{
  struct Library *ExpansionBase, *cs;
  CardDiag *d;
  int present = 0;
  memset(c, 0, sizeof(*c));
  c->arm = -1;
  ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", 0);
  if (ExpansionBase != NULL) {
    present = FindConfigDev(NULL, CSWARP_VID, CSWARP_PID_CTRL) != NULL;
    CloseLibrary(ExpansionBase);
  }
  if (!present) return;
  cs = OpenLibrary((CONST_STRPTR)"cswarp.library", 0);
  if (cs == NULL) return;
  d = (CardDiag *)AllocVec(sizeof(CardDiag), MEMF_PUBLIC | MEMF_CLEAR);
  if (d != NULL) {
    if (cswGetDiagnostic(cs, d) && d->boardName[0]) {
      d->boardName[sizeof(d->boardName) - 1] = 0;
      c->warp = 1;
      strcpy(c->name, d->boardName);
      c->type = d->warpBoardType;
      if (c->type == WBT_WARP7060 || c->type == WBT_WARP1260X) c->arm = 1;
      else if (c->type > 0 && c->type <= WBT_WARP1260X) c->arm = 0;
      else if (name_has(c->name, "7060") || name_has(c->name, "1260X")) c->arm = 1;
    }
    FreeVec(d);
  }
  CloseLibrary(cs);
}
