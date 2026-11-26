#!/usr/bin/env python3
"""Download files of catalog/catalog.txt from raw.githubusercontent.com the way
the Amiga does and compare size + SHA-256 with the catalog.

    python tools/check_raw.py [--all] [catalog.txt]

Without --all: per game the first 3 files, the biggest file, and every file
whose path has a space or that needed CRLF -> LF (git taken).
"""
import argparse
import hashlib
import os
import sys
import urllib.parse
import urllib.request

RAW = 'https://raw.githubusercontent.com'


def read_catalog(path):
    games, g = [], None
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            word, _, rest = line.partition(' ')
            if word == 'GAME':
                g = {'key': rest, 'files': []}
            elif word in ('REPO', 'BRANCH') and g is not None:
                g[word.lower()] = rest
            elif word in ('FILE', 'FILE2') and g is not None:
                size, sha, flag, p = rest.split(' ', 3)
                g['files'].append((p, int(size), sha))
            elif word == 'END' and g is not None:
                games.append(g)
                g = None
    return games


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument('catalog', nargs='?', default=os.path.join(here, 'catalog', 'catalog.txt'))
    ap.add_argument('--all', action='store_true')
    args = ap.parse_args()
    bad = checked = 0
    for g in read_catalog(args.catalog):
        files = g['files']
        if not args.all:
            pick = files[:3] + [max(files, key=lambda f: f[1])]
            pick += [f for f in files if ' ' in f[0] or f[0].endswith(('.txt', '.tsv', 'COPYING'))][:12]
            seen, files = set(), []
            for f in pick:
                if f[0] not in seen:
                    seen.add(f[0])
                    files.append(f)
        for path, size, sha in files:
            url = '%s/%s/%s/releases/amiga/%s' % (RAW, g['repo'], urllib.parse.quote(g['branch']),
                                                  urllib.parse.quote(path))
            with urllib.request.urlopen(urllib.request.Request(
                    url, headers={'User-Agent': 'warphub-check'}), timeout=120) as r:
                data = r.read()
            ok = len(data) == size and hashlib.sha256(data).hexdigest() == sha
            checked += 1
            if not ok:
                bad += 1
                print('DIFFERENT %s %s (%d bytes, catalog %d)' % (g['key'], path, len(data), size))
    print('%d files checked, %d different' % (checked, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
