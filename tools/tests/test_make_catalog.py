"""Tests of make_catalog.py on a fake GitHub (no network)."""
import hashlib
import io
import os
import sys
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import make_catalog as M  # noqa: E402


def make_zip(files):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w') as z:
        for path, data in files.items():
            z.writestr(path, data)
    return buf.getvalue()


class FakeGitHub:
    def __init__(self, repos):
        self.repos = repos          # repo -> dict(branch, zips{name: files}, git{path: data})

    def at(self, r, ref):
        # an older commit of the repo (APPROVED): {'refs': {sha: {zips, git}}}
        return r.get('refs', {}).get(ref, r)

    def api(self, path):
        path, _, query = path.partition('?')
        ref = query[4:] if query.startswith('ref=') else None
        parts = path.split('/')
        repo = parts[2] + '/' + parts[3]
        if repo not in self.repos:
            raise M.NotPublic(path)
        r = self.repos[repo]
        if len(parts) == 7 and parts[4] == 'git':      # git/trees/<ref>
            r = self.at(r, parts[6])
        elif ref:
            r = self.at(r, ref)
        if len(parts) == 4:
            return {'default_branch': r['branch'], 'private': False}
        if parts[4] == 'contents':
            items = [{'name': n, 'type': 'file', 'size': len(make_zip(f))}
                     for n, f in r['zips'].items()]
            items.append({'name': 'Game', 'type': 'dir'})
            return items
        if parts[4] == 'git':
            return {'truncated': False, 'tree': [
                {'type': 'blob', 'path': 'releases/amiga/' + p,
                 'sha': M.git_blob_sha1(d)} for p, d in r['git'].items()]}
        raise AssertionError(path)

    def raw(self, repo, branch, path):
        name = path.rsplit('/', 1)[-1]
        return make_zip(self.at(self.repos[repo], branch)['zips'][name])


GAME_FILES = {
    'Game.info': b'drawer',
    'Game/Game': b'\x00\x00\x03\xf3program',
    'Game/Game.info': b'icon with tooltypes',
    'Game/game.ini': b'[x]\n',
    'Game/doc.info': b'doc drawer',
    'Game/doc/Game_release.txt': b'0.2 new\n',
    'Game/doc/Game_release_en.txt': b'0.2 new\n',
    'Game/data/ReadMe.txt': b'put the data here',
}


def repo_with(files, zips=None, git=None):
    return {'branch': 'main',
            'zips': zips or {'Game-0.1.zip': {'Game/Game': b'old'},
                             'Game-0.2.zip': files},
            'git': git if git is not None else dict(files)}


GAME = {'key': 'game', 'repo': 'me/game',
        'name': {'en': 'Game', 'de': 'Spiel'}, 'desc': {'en': 'A game.', 'pl': 'Gra.'}}


class TestCatalog(unittest.TestCase):
    def build(self, repo, game=GAME):
        logs = []
        return M.build_game(FakeGitHub({'me/game': repo}), game, logs.append), logs

    def test_newest_release_and_hashes(self):
        g, _ = self.build(repo_with(GAME_FILES))
        self.assertEqual(g['version'], '0.2')
        self.assertEqual(g['branch'], 'main')
        files = {p: (s, h, f) for p, s, h, f in g['files']}
        self.assertEqual(set(files), set(GAME_FILES))
        for p, data in GAME_FILES.items():
            self.assertEqual(files[p][0], len(data))
            self.assertEqual(files[p][1], hashlib.sha256(data).hexdigest())
        self.assertEqual(g['notes'], {'pl': 'Game/doc/Game_release.txt',
                                      'en': 'Game/doc/Game_release_en.txt'})
        self.assertEqual(g['readmes'], ['Game/data/ReadMe.txt'])

    def test_flags_keep_player_files(self):
        g, _ = self.build(repo_with(GAME_FILES))
        flags = {p: f for p, s, h, f in g['files']}
        self.assertEqual(flags['Game.info'], 'K')        # drawer icon
        self.assertEqual(flags['Game/Game.info'], 'K')   # program icon
        self.assertEqual(flags['Game/game.ini'], 'K')
        self.assertEqual(flags['Game/doc.info'], 'N')    # no file "Game/doc"
        self.assertEqual(flags['Game/Game'], 'N')

    def test_version_order_is_numeric(self):
        self.assertEqual(M.newest_zip(['A-0.9.zip', 'A-0.10.zip', 'A-0.9.17.zip'])[2], '0.10')

    def test_git_must_match_zip(self):
        git = dict(GAME_FILES)
        git['Game/Game'] = b'rebuilt but not zipped'
        with self.assertRaisesRegex(ValueError, 'git differs'):
            self.build(repo_with(GAME_FILES, git=git))

    def test_files_only_on_one_side_left_out(self):
        git = dict(GAME_FILES, **{'Game/extra': b'x'})
        del git['Game/game.ini']
        g, logs = self.build(repo_with(GAME_FILES, git=git))
        paths = {p for p, s, h, f in g['files']}
        self.assertNotIn('Game/game.ini', paths)        # raw cannot give it
        self.assertNotIn('Game/extra', paths)           # not in the release
        self.assertEqual(sum('WARNING' in line for line in logs), 2)

    def test_crlf_in_zip_takes_git(self):
        zipped = dict(GAME_FILES, **{'Game/doc/Game_release.txt': b'0.2 new\r\n'})
        g, logs = self.build(repo_with(zipped, git=dict(GAME_FILES)))
        files = {p: (s, h) for p, s, h, f in g['files']}
        self.assertEqual(files['Game/doc/Game_release.txt'],
                         (8, hashlib.sha256(b'0.2 new\n').hexdigest()))
        self.assertTrue(any('CRLF' in line for line in logs))

    def test_bad_path_refused(self):
        for bad in ('Game/a  b', 'Game/ a', 'Game/a:b', 'Game/ł'):
            files = dict(GAME_FILES, **{bad: b'x'})
            with self.assertRaisesRegex(ValueError, 'bad path'):
                self.build(repo_with(files))
        for bad in ('Game/../a', 'Game/a/../b'):
            files = dict(GAME_FILES, **{bad: b'x'})
            with self.assertRaisesRegex(ValueError, 'bad path'):
                self.build(repo_with(files))
        files = dict(GAME_FILES, **{'Game/maps/map030.b&b': b'x'})
        g, _ = self.build(repo_with(files))
        self.assertIn('Game/maps/map030.b&b', {p for p, s, h, f in g['files']})
        lines = M.write_catalog([g]).splitlines()
        # FILE2: WARP HUB 0.17 and older refuse a FILE line with '&', but skip unknown lines
        self.assertTrue(any(l.startswith('FILE2 1 ') and l.endswith(' N Game/maps/map030.b&b') for l in lines))
        self.assertFalse(any(l.startswith('FILE ') and '&' in l for l in lines))
        files = dict(GAME_FILES, **{'Game/mods/Floating Numbers - XP/init.lua': b'x'})
        g, _ = self.build(repo_with(files))
        self.assertIn('Game/mods/Floating Numbers - XP/init.lua',
                      {p for p, s, h, f in g['files']})

    def test_not_public_left_out(self):
        logs = []
        self.assertIsNone(M.build_game(FakeGitHub({}), GAME, logs.append))
        self.assertIn('not public', logs[0])

    def test_write_catalog(self):
        g, _ = self.build(repo_with(GAME_FILES))
        text = M.write_catalog([g])
        lines = text.splitlines()
        self.assertEqual(lines[0], 'WARPHUB-CATALOG 1')
        self.assertIn('NAME en Game', lines)
        self.assertIn('NAME de Spiel', lines)
        self.assertNotIn('NAME pl Game', lines)          # same as en: left out
        self.assertIn('DESC pl Gra.', lines)
        self.assertIn('VERSION 0.2', lines)
        self.assertIn('SIZE %d' % sum(len(d) for d in GAME_FILES.values()), lines)
        self.assertIn('FILE %d %s N Game/Game' % (
            len(GAME_FILES['Game/Game']),
            hashlib.sha256(GAME_FILES['Game/Game']).hexdigest()), lines)
        self.assertEqual(lines[-1], 'END')

    def test_tags_with_old_card_groups(self):
        g, _ = self.build(repo_with(GAME_FILES))
        g['tags'] = ['warp', 'other']
        lines = M.write_catalog([g]).splitlines()
        self.assertIn('TAGS warp,other,warp1260,warp1240,warp1260x,warp7060', lines)



OLD_SHA = 'a' * 40


class TestApproved(unittest.TestCase):
    def repo(self):
        r = repo_with(GAME_FILES)                        # the author's newest: 0.2
        old = dict(GAME_FILES, **{'Game/Game': b'\x00\x00\x03\xf3old program'})
        r['refs'] = {OLD_SHA: {'zips': {'Game-0.1.zip': old}, 'git': dict(old)}}
        return r

    def test_approved_release_from_its_commit(self):
        game = dict(GAME, approved={'version': '0.1', 'commit': OLD_SHA})
        g = M.build_game(FakeGitHub({'me/game': self.repo()}), game, lambda m: None)
        self.assertEqual(g['version'], '0.1')
        self.assertEqual(g['branch'], OLD_SHA)          # the Amiga downloads from it
        files = {p: h for p, s, h, f in g['files']}
        self.assertEqual(files['Game/Game'],
                         hashlib.sha256(b'\x00\x00\x03\xf3old program').hexdigest())

    def test_approved_version_not_in_commit_left_out(self):
        logs = []
        game = dict(GAME, approved={'version': '0.3', 'commit': OLD_SHA})
        self.assertIsNone(M.build_game(FakeGitHub({'me/game': self.repo()}), game, logs.append))
        self.assertIn('APPROVED 0.3', logs[-1])


if __name__ == '__main__':
    unittest.main()
