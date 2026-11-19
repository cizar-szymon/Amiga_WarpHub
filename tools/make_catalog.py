#!/usr/bin/env python3
"""Build catalog/catalog.txt: the games WARP HUB on the Amiga can install.

    python tools/make_catalog.py [--games catalog/games.json] [--out catalog/catalog.txt]

For every game in games.json it asks GitHub (the API, only here on the PC,
never on the Amiga) for the newest releases/amiga/<Name>-<version>.zip on the
default branch (or "branch" from games.json), downloads that ZIP once and
writes every file of the game drawer with its size and SHA-256. Every file is
also checked against the git tree of the same branch (the git blob SHA-1), so
the files the Amiga later downloads one by one from

    https://raw.githubusercontent.com/<repo>/<branch>/releases/amiga/<path>

are exactly the files of the ZIP. A game whose repo is not public (404) is
left out with a warning: the Amiga downloads without logging in.

GITHUB_TOKEN in the environment raises the API limit (60 calls an hour
without it); the token is never written anywhere.

The format of catalog.txt (UTF-8, one item per line, the fields split by one
space, the last field of a line may contain spaces):

    WARPHUB-CATALOG 1
    GAME <key>
    NAME <lang> <text>            en always, pl / de when they differ
    DESC <lang> <text>
    REPO <owner>/<repo>
    BRANCH <branch>
    VERSION <version>
    ZIP <name> <size>
    TAGS <tag>,<tag>...           the groups of the filter buttons: new, apps,
                                  warpextreme, arcade, pc, mac, other
    SOURCE local                  only in catalog-test.txt (--simulate): the repo
                                  is not public yet, the files cannot be downloaded
    SIZE <bytes of all files>
    IMAGE <size> <sha256> <path>  the tile picture, relative to catalog.txt
    NOTES <lang> <path>           history of releases (pl, en, de)
    README <path>                 an empty drawer for data the player adds
    FILE <size> <sha256> <N|K> <path>
    END
    ...

Paths are relative to releases/amiga/ ("Settlers.info", "Settlers/Settlers").
Flag K: a file of the player once installed (the program icon with its Tool
Types, *.ini, *.prefs, the drawer icon) - the Amiga writes it only when it is
missing. N: replaced by every update.
"""
import argparse
import hashlib
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

API = 'https://api.github.com'
RAW = 'https://raw.githubusercontent.com'
BASE = 'releases/amiga'
FORMAT = 1
# letters, digits, _.+- and single spaces inside a name (Diablo has
# "Floating Numbers - XP"); the path is the last field of a FILE line
NAME = r'[A-Za-z0-9_.+-]+(?: [A-Za-z0-9_.+-]+)*'
PATH_OK = re.compile(r'^%s(?:/%s)*$' % (NAME, NAME))
KEEP_SUFFIX = ('.ini', '.prefs', '.cfg')
LANGS = ('en', 'pl', 'de')


class NotPublic(Exception):
    pass


class GitHub:
    """The network side; tests replace it."""

    def __init__(self, token=None):
        self.token = token

    def _get(self, url, accept=None, limit=256 << 20):
        headers = {'User-Agent': 'warphub-catalog'}
        if accept:
            headers['Accept'] = accept
        if self.token and url.startswith(API):
            headers['Authorization'] = 'Bearer ' + self.token
        req = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                data = r.read(limit + 1)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise NotPublic(url)
            raise
        if len(data) > limit:
            raise ValueError('too large: ' + url)
        return data

    def api(self, path):
        return json.loads(self._get(API + path, 'application/vnd.github+json'))

    def raw(self, repo, branch, path):
        return self._get('%s/%s/%s/%s' % (RAW, repo, urllib.parse.quote(branch),
                                          urllib.parse.quote(path)))


def version_key(v):
    try:
        return tuple(int(x) for x in v.split('.'))
    except ValueError:
        return None


def git_blob_sha1(data):
    return hashlib.sha1(b'blob %d\0' % len(data) + data).hexdigest()


def newest_zip(names):
    """(name, root, version) of the newest <root>-<version>.zip, or None."""
    best = None
    for n in names:
        m = re.match(r'^([A-Za-z0-9_]+)-(\d+(?:\.\d+)+)\.zip$', n)
        if m and (best is None or version_key(m.group(2)) > version_key(best[2])):
            best = (n, m.group(1), m.group(2))
    return best


def flag_of(path, root, files):
    """K for the files of the player (written only when missing)."""
    name = path.rsplit('/', 1)[-1].lower()
    if path == root + '.info':                       # the drawer icon
        return 'K'
    if name.endswith(KEEP_SUFFIX):
        return 'K'
    if path.endswith('.info') and path.count('/') == 1:
        target = path[:-5]
        if target in files:                          # a program icon (Tool Types)
            return 'K'
    return 'N'


def build_game(gh, game, log=print):
    """One game: a dict for write_catalog, or None when not public."""
    repo = game.get('repo')
    if not repo:
        log('%s: no repo on GitHub yet, left out' % game['key'])
        return None
    try:
        info = gh.api('/repos/' + repo)
    except NotPublic:
        log('%s: repo not public (404), left out' % repo)
        return None
    if info.get('private'):
        log('%s: private, left out' % repo)
        return None
    branch = game.get('branch') or info['default_branch']
    q = '?ref=' + urllib.parse.quote(branch)
    try:
        listing = gh.api('/repos/%s/contents/%s%s' % (repo, BASE, q))
    except NotPublic:
        log('%s: no %s on %s, left out' % (repo, BASE, branch))
        return None
    sizes = {i['name']: i.get('size', 0) for i in listing if i.get('type') == 'file'}
    found = newest_zip(sizes)
    if found is None:
        log('%s: no release ZIP on %s, left out' % (repo, branch))
        return None
    zip_name, root, version = found
    data = gh.raw(repo, branch, BASE + '/' + zip_name)
    root = zip_root(data) or root

    tree = gh.api('/repos/%s/git/trees/%s?recursive=1' % (repo, urllib.parse.quote(branch)))
    if tree.get('truncated'):
        raise ValueError('%s: git tree truncated' % repo)
    prefix = BASE + '/'
    in_git = {}
    for t in tree['tree']:
        if t['type'] != 'blob' or not t['path'].startswith(prefix):
            continue
        rel = t['path'][len(prefix):]
        if rel == root + '.info' or rel.startswith(root + '/'):
            in_git[rel] = t['sha']

    files = zip_files(data, root, zip_name)

    problems = []
    for rel in sorted(files):
        if not PATH_OK.match(rel) or '..' in rel.split('/'):
            problems.append('bad path ' + rel)
        elif rel not in in_git:
            # raw.githubusercontent.com cannot give it: left out
            log('WARNING %s: %s in the ZIP, not in git - left out' % (repo, rel))
            del files[rel]
        elif in_git[rel] != git_blob_sha1(files[rel]):
            # git keeps text with LF; a ZIP made on Windows may have CRLF.
            # The Amiga downloads what git has, so that counts.
            lf = files[rel].replace(b'\r\n', b'\n')
            if in_git[rel] == git_blob_sha1(lf):
                log('%s: %s has CRLF in the ZIP, LF in git (git taken)' % (repo, rel))
                files[rel] = lf
            else:
                problems.append('git differs from the ZIP: ' + rel)
    for rel in sorted(set(in_git) - set(files)):
        log('WARNING %s: %s in git, not in the ZIP - left out' % (repo, rel))
    if problems:
        raise ValueError('%s %s:\n  %s' % (repo, zip_name, '\n  '.join(problems)))
    return game_entry(game, repo, branch, version, (zip_name, sizes[zip_name]), root,
                      files, log)


def zip_root(data):
    """The drawer of the game in a release ZIP (its one top directory)."""
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        tops = {n.split('/', 1)[0] for n in z.namelist() if '/' in n}
    return tops.pop() if len(tops) == 1 else None


def zip_files(data, root, zip_name):
    files = {}
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for zi in z.infolist():
            if zi.is_dir():
                continue
            rel = zi.filename
            if not (rel == root + '.info' or rel.startswith(root + '/')):
                raise ValueError('%s: %s outside %s/' % (zip_name, rel, root))
            files[rel] = z.read(zi)
    return files


def build_local(game, games_dir, log=print):
    """A game from the release ZIP in its local project (simulation of a repo
    that is not public yet: the Amiga sees it, but cannot download it)."""
    import subprocess
    local = os.path.join(games_dir, *game['local'].split('/'))
    rel_dir = os.path.join(local, *BASE.split('/'))
    try:
        names = os.listdir(rel_dir)
    except OSError:
        log('%s: no %s, left out' % (game['key'], rel_dir))
        return None
    found = newest_zip(names)
    if found is None:
        log('%s: no release ZIP in %s, left out' % (game['key'], rel_dir))
        return None
    zip_name, root, version = found
    with open(os.path.join(rel_dir, zip_name), 'rb') as f:
        data = f.read()
    root = zip_root(data) or root
    files = zip_files(data, root, zip_name)
    for rel in sorted(files):
        if not PATH_OK.match(rel) or '..' in rel.split('/'):
            log('WARNING %s: bad path %s - left out' % (game['key'], rel))
            del files[rel]
    try:
        branch = subprocess.run(['git', '-C', local, 'branch', '--show-current'],
                                capture_output=True, text=True).stdout.strip() or 'main'
    except OSError:
        branch = 'main'
    repo = game.get('repo') or 'local/' + game['key']
    g = game_entry(game, repo, branch, version, (zip_name, len(data)), root, files, log)
    g['source'] = 'local'
    g['local_notes'] = {lang: files[path] for lang, path in g['notes'].items()}
    log('  (simulated from %s)' % rel_dir)
    return g


def game_entry(game, repo, branch, version, zip_info, root, files, log):
    notes = {}
    for lang, suffix in (('pl', ''), ('en', '_en'), ('de', '_de')):
        p = '%s/doc/%s_release%s.txt' % (root, root, suffix)
        if p in files:
            notes[lang] = p
    readmes = sorted(p for p in files if p.rsplit('/', 1)[-1] == 'ReadMe.txt'
                     and p.count('/') >= 2)
    entries = [(rel, len(files[rel]), hashlib.sha256(files[rel]).hexdigest(),
                flag_of(rel, root, files)) for rel in sorted(files)]
    log('%s %s: %d files, %d bytes' % (game['key'], version, len(entries),
                                        sum(e[1] for e in entries)))
    return {'key': game['key'], 'name': game.get('name', {}),
            'desc': game.get('desc', {}), 'repo': repo, 'branch': branch,
            'version': version, 'zip': zip_info,
            'notes': notes, 'readmes': readmes, 'files': entries}


def read_blocks(path):
    """GAME ... END blocks of a catalog file, by key (as lines)."""
    blocks, cur = {}, None
    try:
        with open(path, encoding='utf-8') as f:
            for line in f.read().splitlines():
                if line.startswith('GAME '):
                    cur = [line]
                elif cur is not None:
                    cur.append(line)
                    if line == 'END':
                        key = cur[0][5:]
                        blocks[key] = {'key': key, 'block': cur}
                        cur = None
    except OSError:
        pass
    return blocks


def image_of(catalog_dir, key):
    """(size, sha256, path) of images/<key>.png next to the catalog, or None."""
    rel = 'images/%s.png' % key
    try:
        with open(os.path.join(catalog_dir, *rel.split('/')), 'rb') as f:
            data = f.read()
    except OSError:
        return None
    return (len(data), hashlib.sha256(data).hexdigest(), rel)


def one_line(text):
    return ' '.join(str(text).split())


def write_catalog(games):
    out = ['WARPHUB-CATALOG %d' % FORMAT,
           '# made %s by tools/make_catalog.py' % time.strftime('%Y-%m-%d %H:%M', time.gmtime())]
    for g in games:
        if 'block' in g:                      # copied from catalog.txt
            lines = [l for l in g['block'] if not l.startswith(('IMAGE ', 'TAGS '))]
            if g.get('image'):
                lines.insert(-1, 'IMAGE %d %s %s' % g['image'])
            if g.get('tags'):
                lines.insert(1, 'TAGS ' + ','.join(g['tags']))
            out.extend(lines)
            continue
        out.append('GAME ' + g['key'])
        for field, values in (('NAME', g['name']), ('DESC', g['desc'])):
            en = values.get('en')
            if en is None:
                raise ValueError('%s: %s without en' % (g['key'], field))
            for lang in LANGS:
                if lang == 'en' or (values.get(lang) and values[lang] != en):
                    out.append('%s %s %s' % (field, lang, one_line(values.get(lang, en))))
        if g.get('tags'):
            out.append('TAGS ' + ','.join(g['tags']))
        out.append('REPO ' + g['repo'])
        out.append('BRANCH ' + g['branch'])
        out.append('VERSION ' + g['version'])
        if g.get('source'):
            out.append('SOURCE ' + g['source'])
        out.append('ZIP %s %d' % g['zip'])
        out.append('SIZE %d' % sum(f[1] for f in g['files']))
        if g.get('image'):
            out.append('IMAGE %d %s %s' % g['image'])
        for lang in LANGS:
            if lang in g['notes']:
                out.append('NOTES %s %s' % (lang, g['notes'][lang]))
        for r in g['readmes']:
            out.append('README ' + r)
        for path, size, sha, flag in g['files']:
            out.append('FILE %d %s %s %s' % (size, sha, flag, path))
        out.append('END')
    return '\n'.join(out) + '\n'


def main(argv=None):
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--games', default=os.path.join(here, 'catalog', 'games.json'))
    ap.add_argument('--out', default=os.path.join(here, 'catalog', 'catalog.txt'))
    ap.add_argument('--only', help='comma separated keys (for tests)')
    ap.add_argument('--simulate', action='store_true',
                    help='games that are not public yet from the local release ZIPs '
                         '(catalog/catalog-test.txt; SOURCE local)')
    args = ap.parse_args(argv)
    if args.simulate and args.out == os.path.join(here, 'catalog', 'catalog.txt'):
        args.out = os.path.join(here, 'catalog', 'catalog-test.txt')
    with open(args.games, encoding='utf-8') as f:
        cfg = json.load(f)
    gh = GitHub(os.environ.get('GITHUB_TOKEN'))
    public = {}
    if args.simulate:
        public = read_blocks(os.path.join(here, 'catalog', 'catalog.txt'))
    only = set(args.only.split(',')) if args.only else None
    games = []
    for game in cfg['games']:
        if only and game['key'] not in only:
            continue
        if args.simulate:
            # public games as in the checked catalog.txt (no GitHub API calls),
            # the others from their local release ZIPs
            g = public.get(game['key'])
            if g is None and game.get('local'):
                g = build_local(game, os.path.dirname(here))
        else:
            g = build_game(gh, game)
        if g:
            g['image'] = image_of(os.path.dirname(args.out), game['key'])
            g['tags'] = game.get('tags', [])
            for lang, data in g.get('local_notes', {}).items():
                # the Amiga cannot get them from GitHub: notes/<key>_<lang>.txt
                d = os.path.join(os.path.dirname(args.out), 'notes')
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, '%s_%s.txt' % (g['key'], lang)), 'wb') as f:
                    f.write(data)
            games.append(g)
    text = write_catalog(games)
    tmp = args.out + '.tmp'
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    os.replace(tmp, args.out)
    print('%s: %d games' % (args.out, len(games)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
