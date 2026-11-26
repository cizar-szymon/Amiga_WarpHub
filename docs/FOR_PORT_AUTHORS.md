# Your port in WARP HUB

**WARP HUB** is a Workbench program for Amigas with a Warp card. It shows Amiga ports in
one window, installs them and keeps them up to date, straight from GitHub over HTTPS
(AmiSSL 5; WarpSSL on Warp cards). This page is for authors who keep their Amiga port on
GitHub and want it in WARP HUB.

You do not need anything of WARP HUB in your program. WARP HUB only needs a release
in a fixed layout in your repository. The rules are below.

## How WARP HUB gets your port

1. Every hour a job in this repository (`.github/workflows/catalog.yml`) looks into your
   repository: it finds the newest `releases/amiga/<Name>-<version>.zip`, reads it and
   writes every file with its size and SHA-256 into the catalog (`catalog/catalog.txt`).
2. On the Amiga, WARP HUB reads the catalog and downloads the files **one by one, from git**:
   `https://raw.githubusercontent.com/<owner>/<repo>/<branch>/releases/amiga/<path>`.
   No ZIP is downloaded and no UnZip is needed on the Amiga.
3. Each file is checked against its SHA-256. A file that does not match is not installed.
4. An update downloads only the files that changed.

So **the unpacked drawer in git must hold exactly the files of the ZIP, byte for byte.**
Most of the rules below follow from this.

## Checklist

- [ ] The repository is **public**.
- [ ] `releases/amiga/` holds the current ZIP, the same files unpacked and the drawer icon.
- [ ] The ZIP name is `<Name>-<version>.zip`. `<Name>` uses only letters, digits and `_`.
      The version is numbers with dots (`0.9`, `1.2.3`).
- [ ] `.gitattributes` has `releases/** -text`.
- [ ] Every file of the ZIP is in git (watch out for `.gitignore`).
- [ ] The main program has `$VER: <Name> <version>`, with the same version as the ZIP name.
- [ ] No game data, ROMs, converted music or other files you may not give away are in
      `releases/`.
- [ ] Optional: release history `doc/<Name>_release*.txt` and the guide `doc/Install.txt`.
- [ ] You asked for your port to be added (see the end of this page).

## 1. The layout of the release

```
releases/amiga/<Name>.info           the icon of the game drawer
releases/amiga/<Name>-<version>.zip  the current release (only one ZIP here)
releases/amiga/<Name>/               the same files as in the ZIP, unpacked
    <Name>, <Name>.info              the program and its icon (Tool Types)
    <name>.ini, ...                  settings, if the program has them
    doc/, doc.info                   texts for the player
    data/ReadMe.txt                  empty drawers for data the player adds
```

- The ZIP has **one top drawer** `<Name>/` (and optionally `<Name>.info`). Files outside it
  are refused.
- Keep **only the current ZIP** in `releases/amiga/`. WARP HUB takes the highest version.
  Older ZIPs only make the repository bigger.
- Names of files and drawers can use letters, digits, `_ - . +` and single spaces inside
  a name. No `..` and no other characters. A file with another name is left out.
- Single files in git must stay under 100 MB, the GitHub limit.
- The release can be on any branch. Tell us if it is not the default branch.

## 2. Git must hold the files byte for byte

Add this line to `.gitattributes` in your repository:

```
releases/** -text
```

Without it, git on Windows (`core.autocrlf`) can change the line ends of text files. The
player then gets files that differ from your ZIP. One port had index files that pointed to
byte positions in CRLF text files, while git kept the texts with LF: every text of the game
would have been shifted. If you add the line to a repository that already has a release,
run once:

```
git add --renormalize releases/
```

**Everything in the ZIP must be in git.** A `.gitignore` rule such as `data/` also hides
`releases/amiga/<Name>/data/ReadMe.txt`. Add such files with `git add -f` or with an
exception in `.gitignore`.

Before you push a release, you can check it with `tools/check_release.py` from this
repository:

```
python tools/check_release.py <your repository drawer>
```

It compares every file of the newest ZIP with git and the disk, and checks
`.gitattributes`. It must end with "all the same in git".

## 3. Version: `$VER`

Put the version in the main program, as in any Amiga program:

```c
const char version_tag[] = "$VER: MyGame 0.42 (20.11.2026)";
```

The number must be the same as in the ZIP name (`MyGame-0.42.zip`). WARP HUB reads it to
see whether the game is installed and which version it is, also when the player installed
it by hand. It then shows "Installed 0.42" or "New version".

How WARP HUB finds the main program:

- It is a file directly in `<Name>/` with its icon `.info` next to it.
- If there are several (a game and a tool), the largest file counts.
- The **Run** button of WARP HUB starts this program like a double click on its icon, with
  its Tool Types.

The game drawer on the player's disk should keep the name from the package (`MyGame`, not
`MyGame-test`). Otherwise WARP HUB does not find it.

## 4. Files of the player

WARP HUB writes these files **only when they are missing**. An update never replaces them:

- the drawer icon `<Name>.info`;
- program icons: the player may have changed their Tool Types;
- `*.ini`, `*.prefs`, `*.cfg`.

Every other file in the release is replaced when it changes.

So **a new option must have a default value in the program.** The player's old `.ini`
does not know the new option. Files the program writes at run time (saves, high scores)
are not in the release, so WARP HUB never touches them.

## 5. Game data and ROMs

Never put the original game data, ROMs, converted music or private translations into
`releases/`. WARP HUB would give them to every player.

Instead, leave the drawers empty and put a `ReadMe.txt` in each one. Say plainly which
files go there, where to get them and how they must be named:

```
releases/amiga/MyGame/data/ReadMe.txt
```

WARP HUB shows these texts in its **"How to start"** window. If you have
`<Name>/doc/Install.txt`, that file is shown instead. Use it when starting needs more
steps than copying files.

## 6. Texts for the WARP HUB window (optional)

| File | What WARP HUB does with it |
|---|---|
| `<Name>/doc/<Name>_release_en.txt` | release history, English |
| `<Name>/doc/<Name>_release.txt` | release history, Polish |
| `<Name>/doc/<Name>_release_de.txt` | release history, German |
| `<Name>/doc/Install.txt` | the "How to start" window |

The history is shown next to the game in the player's language, or in English when that
language is missing. Plain text in UTF-8 (or plain ASCII), lines of about 70 characters. Write
the newest release first. Lines of a paragraph are joined and wrapped to the window.

The name, the description and the requirements shown in WARP HUB are **not** taken from
your repository. They are in the WARP HUB catalog, in English, Polish and German. You
send them to us when you ask for your port to be added.

## 7. A program instead of a game (optional)

WARP HUB normally puts a game into its own drawer (`Games/MyGame/`). A program that must
lie in a shared drawer, next to other programs, can be marked `FLAT` in the catalog. Then
only the files from inside `<Name>/` are installed, without `doc/` and the drawer icon,
and the old program is kept as `<program>.old`. Tell us if your program needs this.

## 8. New releases

- **Your own release, after it is approved.** For a port in someone else's repository,
  WARP HUB keeps the release that was checked (`APPROVED <version> <commit>` in the
  catalog). Your next release reaches the players when we approve it. Tell us when a new
  release is out (an issue in this repository).
- **After your push:** the catalog job runs every hour. GitHub then keeps `raw` files in
  its cache for up to 5 minutes. On the Amiga: **Refresh**, then the new version shows up.
  **Update** downloads only the changed files.
- **Do not rewrite** the history of a commit that holds an approved release (no
  `push --force` over it). WARP HUB downloads from that exact commit.

A release that cannot be kept on the branch of your code (for example, the release needs
files the code branch does not have) can live on a separate branch with only
`releases/amiga/`. Tell us its name. Do not merge it into your main branch.

## 9. How to get your port added

Open an issue in **https://github.com/cizar-szymon/Amiga_WarpHub/issues** with:

1. the link to your repository (and the branch, if the release is not on the default one);
2. the name of the game or program, and one or two sentences of description (English;
   Polish and German too, if you can);
3. the requirements, one short line each: CPU (`68040 / 68060`), clock (`100 MHz
   recommended`), memory (`32 MB Fast RAM`), system and screen (`AmigaOS 3.2, RTG 16-bit`),
   data the player needs (`Data: original game files`);
4. where it runs (the groups of WARP HUB, several may apply): WARP (natively on the Warp
   cards, 68040 / 68060), WARP_ARM (uses the ARM processor with much memory of the
   Warp 1260X / 7060, e.g. switched on by an option or in the game menu), PPC (PowerPC),
   other, programs;
5. a picture: a title screen or a typical scene. WARP HUB shows it at 224x140 pixels in
   256 colours; we scale it.

We check the release, add the port to the catalog and approve the current release. It
then appears in WARP HUB after the next hourly run.

## Questions

Open an issue in https://github.com/cizar-szymon/Amiga_WarpHub/issues.
