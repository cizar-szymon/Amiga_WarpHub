#!/usr/bin/env python3
"""Prepare the private source of WARP HUB: the full packages (with the data of
the games) for the Amigas of the owner, served by tools/private_server.py.

    python tools/make_private.py [--root ../WarpHub-private (next to WarpHub)] [--only settlers,vastar]

For every game of catalog/games/ with a LOCAL project drawer it takes the
newest private/<Name>-<version>-full.zip of that project (not the -pc-full
one), unpacks it into <root>/www/files/<key>/ (only files that changed) and
writes <root>/www/catalog.txt in the format of the public catalog with
"SOURCE private": the Amiga gets the files from

    https://<server>/files/<key>/<path>

after logging in. Names, descriptions, groups and requirements are those of
the public catalog; the drawer icon (<Root>.info), which the full ZIPs do not
have, comes from releases/amiga/ of the project. The picture is the one of
catalog/images/.

The private root never goes into a repo: it holds the data of the games.
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_files  # noqa: E402
import make_catalog  # noqa: E402
import paths  # noqa: E402

FULL = re.compile(r'^(.+)-(\d+(?:\.\d+)*)-full\.zip$')
CHUNK = 1 << 20


def newest_full(private_dir, name=None, lower=False):
    best = None
    try:
        names = os.listdir(private_dir)
    except OSError:
        return None
    for n in names:
        m = FULL.match(n)
        if m is None or m.group(1).endswith('-pc'):
            continue
        if name and (m.group(1).lower() if lower else m.group(1)) != name:
            continue
        key = make_catalog.version_key(m.group(2))
        if best is None or key > best[0]:
            best = (key, n, m.group(1), m.group(2))
    return best and best[1:]


def sha_of_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(CHUNK), b''):
            h.update(block)
    return h.hexdigest()


def unpack(zip_path, dest, extra, log):
    """Files of the ZIP (and extra {path: bytes}) into dest; returns
    {path: (size, sha256)}. A file already there with the same size and
    SHA-256 is not written again (the copy to the server sends only changes)."""
    out = {}
    with zipfile.ZipFile(zip_path) as z:
        for zi in z.infolist():
            if zi.is_dir():
                continue
            rel = zi.filename
            if not make_catalog.PATH_OK.match(rel) or '..' in rel.split('/'):
                log('  WARNING bad path %s - left out' % rel)
                continue
            target = os.path.join(dest, *rel.split('/'))
            h = hashlib.sha256()
            with z.open(zi) as src:
                for block in iter(lambda: src.read(CHUNK), b''):
                    h.update(block)
            sha = h.hexdigest()
            if not (os.path.exists(target) and os.path.getsize(target) == zi.file_size
                    and sha_of_file(target) == sha):
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with z.open(zi) as src, open(target + '.tmp', 'wb') as f:
                    shutil.copyfileobj(src, f, CHUNK)
                os.replace(target + '.tmp', target)
            out[rel] = (zi.file_size, sha)
    for rel, data in extra.items():
        if rel in out:
            continue
        target = os.path.join(dest, *rel.split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'wb') as f:
            f.write(data)
        out[rel] = (len(data), hashlib.sha256(data).hexdigest())
    return out


def build(game, projects, www, log):
    local = os.path.join(projects, *game['local'].split('/'))
    pdir = os.path.join(local, 'private')
    # two games in one project (C&C): the full ZIP whose name is the key of the game
    found = ((game.get('fullname') and newest_full(pdir, game['fullname']))
             or newest_full(pdir, game['key'], lower=True) or newest_full(pdir))
    if found is None:
        log('%s: no private/*-full.zip, left out' % game['key'])
        return None
    zip_name, root, version = found
    zip_path = os.path.join(local, 'private', zip_name)
    with zipfile.ZipFile(zip_path) as z:
        tops = {n.split('/')[0] for n in z.namelist()}
    if len(tops) == 1:
        root = tops.pop()
    extra = {}
    icon = os.path.join(local, 'releases', 'amiga', root + '.info')
    if not os.path.exists(icon):
        icon = os.path.join(local, 'releases-ra', 'amiga', root + '.info')
    if os.path.exists(icon):
        with open(icon, 'rb') as f:
            extra[root + '.info'] = f.read()
    dest = os.path.join(www, 'files', game['key'])
    files = unpack(zip_path, dest, extra, log)
    names = set(files)
    entries = [(rel, files[rel][0], files[rel][1], make_catalog.flag_of(rel, root, names))
               for rel in sorted(files)]
    notes = {}
    for lang, suffix in (('pl', ''), ('en', '_en'), ('de', '_de')):
        p = '%s/doc/%s_release%s.txt' % (root, root, suffix)
        if p in files:
            notes[lang] = p
    readmes = sorted(p for p in files if p.rsplit('/', 1)[-1] == 'ReadMe.txt'
                     and p.count('/') >= 2)
    log('%s %s (full): %d files, %d bytes' % (game['key'], version, len(entries),
                                               sum(e[1] for e in entries)))
    return {'key': game['key'], 'name': game.get('name', {}), 'desc': game.get('desc', {}),
            'repo': game.get('repo') or 'private/' + game['key'], 'branch': 'private',
            'version': version, 'zip': (zip_name, os.path.getsize(zip_path)),
            'source': 'private', 'notes': notes, 'readmes': readmes, 'files': entries,
            'tags': game.get('tags', []), 'req': game.get('req', {})}


def main(argv=None):
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    projects = paths.GAMES
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=os.path.join(os.path.dirname(here), 'WarpHub-private'))
    ap.add_argument('--games', default=os.path.join(here, 'catalog', 'games'))
    ap.add_argument('--only', help='comma separated keys')
    args = ap.parse_args(argv)
    www = os.path.join(args.root, 'www')
    os.makedirs(www, exist_ok=True)
    only = set(args.only.split(',')) if args.only else None
    games = []
    for game in game_files.load_games(args.games):
        if not game.get('local') or (only and game['key'] not in only):
            continue
        g = build(game, projects, www, print)
        if g is None:
            continue
        if game.get('flat'):
            make_catalog.apply_flat(g)          # WARP HUB itself: a file in SYS:WarpApps
        img = os.path.join(here, 'catalog', 'images', game['key'] + '.png')
        if os.path.exists(img):
            os.makedirs(os.path.join(www, 'images'), exist_ok=True)
            shutil.copyfile(img, os.path.join(www, 'images', game['key'] + '.png'))
            g['image'] = make_catalog.image_of(www, game['key'])
        games.append(g)
    text = make_catalog.write_catalog(games).replace(
        '# made by tools/make_catalog.py from catalog/games/ (do not edit)',
        '# private: made by tools/make_private.py (do not edit, never in a repo)')
    with open(os.path.join(www, 'catalog.txt.tmp'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    os.replace(os.path.join(www, 'catalog.txt.tmp'), os.path.join(www, 'catalog.txt'))
    print('%s: %d games' % (os.path.join(www, 'catalog.txt'), len(games)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
