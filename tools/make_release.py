#!/usr/bin/env python3
"""The release of WARP HUB in the layout of the game ports (PORTING_PLAN.md,
phase 5), so the catalog can list WARP HUB itself (group Programs) and it can
update itself:

    releases/amiga/WarpHub.info          drawer icon (the WARP drawer)
    releases/amiga/WarpHub-<v>.zip       the release
    releases/amiga/WarpHub/              the same unpacked:
        WarpHub, WarpHub.info            program and its icon
        doc/, doc.info                   player texts PL/EN/DE, history of releases

    python tools/make_release.py          (after sh tools/build_amiga.sh)

The version is WARPHUB_VERSION in amiga/warphub.c; the program must carry the
same "$VER: WarpHub <v>" or nothing is made. WarpHub.prefs and cache/ are the
player's and never in the release. Older ZIPs are removed from releases/amiga
(git keeps them in the history).
"""
import os
import re
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import paths  # noqa: E402
GAMES = paths.GAMES
OUT = os.path.join(HERE, 'releases', 'amiga')
ROOT = 'WarpHub'
# the icons of the doc drawer and of the texts (MultiView), from OpenDUNE
ICONS = os.path.join(GAMES, 'OpenDUNE', 'releases', 'amiga', 'OpenDUNE')
TEXTS = [  # (file in docs/release/amiga, icon of OpenDUNE's doc drawer to use)
    ('WarpHub.txt', 'doc/OpenDUNE.txt.info'),
    ('WarpHub_en.txt', 'doc/OpenDUNE_en.txt.info'),
    ('WarpHub_de.txt', 'doc/Install.txt.info'),
    ('WarpHub_release.txt', 'doc/OpenDUNE_release.txt.info'),
    ('WarpHub_release_en.txt', 'doc/OpenDUNE_release_en.txt.info'),
    ('WarpHub_release_de.txt', 'doc/OpenDUNE_release.txt.info'),
]
EXECUTABLES = {'WarpHub'}


def version():
    src = open(os.path.join(HERE, 'amiga', 'warphub.c'), encoding='utf-8').read()
    return re.search(r'#define WARPHUB_VERSION "([0-9.]+)"', src).group(1)


def put(z, path, arcname):
    """with Unix permissions: Amiga UnZip makes them the protection bits"""
    info = zipfile.ZipInfo.from_file(path, arcname)
    info.create_system = 3
    mode = 0o755 if os.path.basename(arcname) in EXECUTABLES else 0o644
    info.external_attr = (0o100000 | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    with open(path, 'rb') as f:
        z.writestr(info, f.read())


def put_dir(z, arcname):
    info = zipfile.ZipInfo(arcname.rstrip('/') + '/')
    info.create_system = 3
    info.external_attr = (0o040755 << 16) | 0x10
    z.writestr(info, '')


def main():
    v = version()
    prog = os.path.join(HERE, 'build', 'amiga', 'WarpHub')
    data = open(prog, 'rb').read()
    if ('$VER: WarpHub %s ' % v).encode() not in data:
        sys.exit('build/amiga/WarpHub has no "$VER: WarpHub %s" - build it first' % v)

    tree = os.path.join(OUT, ROOT)
    if os.path.exists(tree):
        shutil.rmtree(tree)
    os.makedirs(os.path.join(tree, 'doc'))
    shutil.copyfile(prog, os.path.join(tree, 'WarpHub'))
    shutil.copyfile(os.path.join(HERE, 'amiga', 'icon', 'WarpHub.info'),
                    os.path.join(tree, 'WarpHub.info'))
    shutil.copyfile(os.path.join(ICONS, 'doc.info'), os.path.join(tree, 'doc.info'))
    for name, icon in TEXTS:
        shutil.copyfile(os.path.join(HERE, 'docs', 'release', 'amiga', name),
                        os.path.join(tree, 'doc', name))
        shutil.copyfile(os.path.join(ICONS, icon), os.path.join(tree, 'doc', name + '.info'))
    shutil.copyfile(os.path.join(GAMES, 'WARP-drawer.info'), os.path.join(OUT, ROOT + '.info'))

    for old in os.listdir(OUT):
        if re.match(r'^WarpHub-[0-9.]+\.zip$', old) and old != 'WarpHub-%s.zip' % v:
            os.remove(os.path.join(OUT, old))
    zpath = os.path.join(OUT, 'WarpHub-%s.zip' % v)
    with zipfile.ZipFile(zpath, 'w') as z:
        put(z, os.path.join(OUT, ROOT + '.info'), ROOT + '.info')
        put_dir(z, ROOT)
        for dirpath, dirs, files in os.walk(tree):
            dirs.sort()
            rel = os.path.relpath(dirpath, OUT).replace(os.sep, '/')
            if rel != ROOT:
                put_dir(z, rel)
            for f in sorted(files):
                put(z, os.path.join(dirpath, f), rel + '/' + f)
    n = sum(1 for i in zipfile.ZipFile(zpath).infolist() if not i.is_dir())
    print('%s: %d files, %d bytes' % (zpath, n, os.path.getsize(zpath)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
