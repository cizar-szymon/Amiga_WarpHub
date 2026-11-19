#!/usr/bin/env python3
"""catalog/games/<key>.txt: one game or program of WARP HUB, written by hand
or by the web editor (editor/index.html). make_catalog.py and make_images.py
read them all; the file name is the key.

UTF-8 text, one item per line, "#" starts a comment:

    NAME <lang> <text>        en always, pl / de when they differ
    DESC <lang> <text>        one line per language
    TAGS <tag>,<tag>...       apps, warp1260 (Warp 560/1260), warp1240,
                              warp1260x, warp7060, ppc (PowerPC), other
    REQ <lang> <text>         one line of the requirements (several lines)
    REPO <owner>/<repo>       the GitHub repo with releases/amiga
    BRANCH <branch>           when the release is not on the default branch
    APPROVED <version> <commit>
                              only this release goes to the players: the
                              files are taken from that commit of the repo,
                              a newer release of the author waits until it
                              is approved (without the line: always the newest)
    FLAT                      a program that goes straight into the chosen
                              drawer (WARP HUB in SYS:WarpApps): the files of
                              <Root>/ only; its doc/ and drawer icon are for
                              the window, not installed
    ORDER <number>            place in the gallery (smaller first)
    LOCAL <drawer>            PC only: the project drawer (test catalog)
    IMAGE-SOURCE <path> [x0 y0 x1 y1] [pixel]
                              PC only: where make_images.py cuts the picture
                              from (path from the AmigaGAMES drawer)

The picture itself is catalog/images/<key>.png.
"""
import os
import re

KEY_OK = re.compile(r'^[a-z0-9][a-z0-9_-]{0,30}$')
LANGS = ('en', 'pl', 'de')


def parse(key, text):
    g = {'key': key, 'name': {}, 'desc': {}, 'tags': [], 'req': {}, 'order': 1000}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        word, _, rest = line.partition(' ')
        rest = rest.strip()
        if word in ('NAME', 'DESC', 'REQ'):
            lang, _, value = rest.partition(' ')
            if lang not in LANGS or not value.strip():
                raise ValueError('%s: bad line: %s' % (key, line))
            if word == 'REQ':
                g['req'].setdefault(lang, []).append(value.strip())
            else:
                g[word.lower()][lang] = value.strip()
        elif word == 'TAGS':
            g['tags'] = [t for t in re.split(r'[,\s]+', rest) if t]
        elif word == 'REPO':
            g['repo'] = rest or None
        elif word == 'BRANCH':
            g['branch'] = rest
        elif word == 'APPROVED':
            parts = rest.split()
            if len(parts) != 2 or not re.match(r'^[0-9a-f]{40}$', parts[1]):
                raise ValueError('%s: APPROVED <version> <40 hex commit>: %s' % (key, line))
            g['approved'] = {'version': parts[0], 'commit': parts[1]}
        elif word == 'FLAT' and not rest:
            g['flat'] = True
        elif word == 'ORDER':
            g['order'] = int(rest)
        elif word == 'LOCAL':
            g['local'] = rest
        elif word == 'IMAGE-SOURCE':
            # the path may contain spaces ("Miner 2049er/..."): the crop
            # numbers and "pixel" are taken from the end
            parts = rest.split(' ')
            spec = {}
            if parts and parts[-1] == 'pixel':
                spec['pixel'] = True
                parts.pop()
            if len(parts) > 4 and all(re.match(r'^-?\d+$', p) for p in parts[-4:]):
                spec['crop'] = [int(p) for p in parts[-4:]]
                parts = parts[:-4]
            spec['src'] = ' '.join(parts)
            g['image'] = spec
        else:
            raise ValueError('%s: unknown line: %s' % (key, line))
    if 'en' not in g['name']:
        raise ValueError('%s: NAME en is missing' % key)
    return g


def load_games(directory):
    """All games of the drawer, in gallery order."""
    games = []
    for fn in sorted(os.listdir(directory)):
        if not fn.endswith('.txt'):
            continue
        key = fn[:-4]
        if not KEY_OK.match(key):
            raise ValueError('bad file name: %s (a-z, 0-9, _ and -)' % fn)
        with open(os.path.join(directory, fn), encoding='utf-8') as f:
            games.append(parse(key, f.read()))
    games.sort(key=lambda g: (g['order'], g['key']))
    return games


def write(g):
    """The text of one game file (the inverse of parse)."""
    out = ['# WARP HUB: %s (format: tools/game_files.py)' % g['name'].get('en', g['key'])]
    for lang in LANGS:
        if lang in g.get('name', {}):
            out.append('NAME %s %s' % (lang, g['name'][lang]))
    for lang in LANGS:
        if lang in g.get('desc', {}):
            out.append('DESC %s %s' % (lang, g['desc'][lang]))
    if g.get('tags'):
        out.append('TAGS ' + ','.join(g['tags']))
    for lang in LANGS:
        for r in g.get('req', {}).get(lang, []):
            out.append('REQ %s %s' % (lang, r))
    if g.get('repo'):
        out.append('REPO ' + g['repo'])
    if g.get('branch'):
        out.append('BRANCH ' + g['branch'])
    if g.get('approved'):
        out.append('APPROVED %s %s' % (g['approved']['version'], g['approved']['commit']))
    if g.get('flat'):
        out.append('FLAT')
    out.append('ORDER %d' % g.get('order', 1000))
    if g.get('local'):
        out.append('LOCAL ' + g['local'])
    if g.get('image'):
        s = g['image']
        line = 'IMAGE-SOURCE ' + s['src']
        if s.get('crop'):
            line += ' ' + ' '.join(str(n) for n in s['crop'])
        if s.get('pixel'):
            line += ' pixel'
        out.append(line)
    return '\n'.join(out) + '\n'
