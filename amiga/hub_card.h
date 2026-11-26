/* hub_card - which Warp card is in this Amiga (cswarp.library, only read).
 * WARP HUB dims the tiles of games that cannot run on it. */
#ifndef HUB_CARD_H
#define HUB_CARD_H

typedef struct HubCard {
  int warp;          /* 1: a Warp card answered (cswarp.library) */
  int arm;           /* 1: it has the ARM with much memory (Warp 1260X, 7060),
                        0: it has not, -1: not known (nothing is dimmed for it) */
  int type;          /* warpBoardType (WarpBoardType.h), 0 = not reported */
  char name[32];     /* boardName, "" without a card */
} HubCard;

/* Asks cswarp.library once (without a card or the library: warp = 0). */
void hub_card_read(HubCard *c);

#endif
