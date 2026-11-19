# WARP HUB for the Amiga with a Warp card

**WARP HUB** is a Workbench program that shows our Amiga ports in one window and installs and
updates them: a gallery with a picture of each game, its description, requirements, the history
of releases and a button to install or update. The games come straight from their GitHub
repositories over HTTPS, file by file, each one checked with SHA-256.

**It is made for the Amiga 500 / 1200 / 3000 / 4000 with a Warp accelerator card: Warp 560,
Warp 1240, Warp 1260, Warp 1260 Extreme or Warp 7060.** On a Warp with firmware 3.074 or later,
**WarpSSL** does the encryption of AmiSSL on the card, so the downloads are faster and leave the
CPU free. It was written and tested on an A4000 with a Warp 7060 (68060). Other Amigas with
AmigaOS 3.2, AmiSSL 5 and a network connection run it too, without the speed of WarpSSL.

- a gallery of the games and programs with pictures, filters: Programs, Warp 560/1260,
  Warp 1240, Warp 1260X, Warp 7060, PowerPC, Other
- English, Polish and German (the window, the descriptions and, where the game has them, the
  release notes)
- **Install**: you pick a drawer, the game gets its own drawer with the WARP drawer icon
- **Update**: only the files that changed are downloaded; the old program goes to `old/`
- **your files stay**: the game data you added, settings (`*.ini`, `*.prefs`), saves and the
  program icon with your Tool Types are never overwritten
- **How to start**: a window with what the game needs from you (for example the data files of
  the original game) and where to put it; it opens by itself after an installation
- **Games drawer / Programs drawer**: WARP HUB looks there at every start and recognises the
  installed games and their versions (from `$VER`), also the ones installed by hand
- fits a 640x480 Workbench; on larger screens the pictures are bigger

The game data, ROMs and music of the original games are not distributed - WARP HUB says what
you need and where to put it.

## Requirements

- AmigaOS 3.2 (the window uses the ReAction classes of 3.2)
- a 68040 or 68060 (Warp card), RTG graphics (Picasso96)
- **AmiSSL 5** (`amisslmaster.library`); WarpSSL on Warp cards with firmware 3.074+
- a TCP/IP stack with an internet connection (Roadshow, warpSOCKET)
- `png.datatype` for the pictures

The Amiga's clock does not have to be right: WARP HUB gets the time from an NTP server only to
check the certificates (`pool.ntp.org`, `time.cloudflare.com`, `time.google.com`).

## Settings

`WarpHub.prefs` next to the program, a text file:

```
LANGUAGE pl                   en / pl / de (also chosen in the window)
GAMESDIR Work:Games           the drawer of your games
APPSDIR Work:Programs         the drawer of your programs
CATALOG <URL or file>         another catalog (only for tests)
INSTALLED <game> <version> <drawer>    written by WARP HUB
```

Tool Types of the icon (and Shell arguments): `LANG=pl|en|de`, `CATALOG=<URL or file>`.

Keys: left / right choose a game, up / down and the mouse wheel scroll, 1-8 the filters,
Help opens "How to start", Esc quits.

## How it works

The catalog `catalog/catalog.txt` in this repository lists every game: name, description and
requirements in three languages, the repository and branch, the version, and every file of the
release (`releases/amiga/<Game>/` in the game's repository) with its size and SHA-256. WARP HUB
reads it from `raw.githubusercontent.com` (no GitHub API, so no request limit), compares the
files with the ones on your disk and downloads only what is missing or changed.

Every game or program is one small text file, `catalog/games/<key>.txt` (name, description,
groups and requirements in three languages, the repository; format in `tools/game_files.py`),
with its picture `catalog/images/<key>.png`. They can be edited in the browser:

**https://cizar-szymon.github.io/Amiga_WarpHub/** - the editor (`editor/`) writes one commit with
the game file and the picture (a fine-grained token with *Contents: Read and write* on this
repository is needed to save).

GitHub Actions (`.github/workflows/catalog.yml`) then builds `catalog/catalog.txt` from the
game files and the newest releases in the games' repositories, and brings the pictures to
224x140 / 256 colours - after every change and once a day. The same on the PC:

```
python tools/make_catalog.py          # GITHUB_TOKEN raises the GitHub API limit
python tools/make_images.py           # pictures from IMAGE-SOURCE (PC only)
python -m unittest discover -s tools/tests
```

## Building

bebbo's m68k-amigaos-gcc in Docker (`sacredbanana/amiga-compiler:m68k-amigaos`) and the AmiSSL 5
SDK:

```
sh tools/build_amiga.sh               # -> build/amiga/WarpHub, WarpHubCLI
```

`WarpHubCLI` is the same engine for the Shell: `LIST`, `INFO <game>`, `CHECK <game> TO <drawer>`,
`INSTALL <game> TO <drawer>`, `TIME`.

The program icon is made by `tools/make_hub_icon.py`.
