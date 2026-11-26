import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import game_files as G  # noqa: E402

TEXT = '''# a comment
NAME en Miner 2049er
NAME pl Górnik
DESC en Bounty Bob claims every floor.
DESC pl Bounty Bob zajmuje każde piętro.
TAGS other, new
REQ en 68020 or better
REQ en tested on 060 / 50 MHz
REQ pl 68020 lub lepszy
REPO cizar-szymon/Amiga_Game_Miner-2049er
BRANCH amiga
FLAT
APPROVED 0.9 0123456789abcdef0123456789abcdef01234567
ORDER 40
LOCAL Miner 2049er
IMAGE-SOURCE Miner 2049er/build/port/lv/levels.png 0 0 400 250 pixel
'''


class GameFiles(unittest.TestCase):
    def test_parse(self):
        g = G.parse('miner', TEXT)
        self.assertEqual(g['name'], {'en': 'Miner 2049er', 'pl': 'Górnik'})
        self.assertEqual(g['tags'], ['other', 'new'])
        self.assertEqual(g['req']['en'], ['68020 or better', 'tested on 060 / 50 MHz'])
        self.assertEqual(g['branch'], 'amiga')
        self.assertEqual(g['order'], 40)
        self.assertTrue(g['flat'])
        self.assertEqual(g['approved'], {'version': '0.9',
                                         'commit': '0123456789abcdef0123456789abcdef01234567'})
        self.assertEqual(g['image'], {'src': 'Miner 2049er/build/port/lv/levels.png',
                                      'crop': [0, 0, 400, 250], 'pixel': True})

    def test_write_parse_round_trip(self):
        g = G.parse('miner', TEXT)
        self.assertEqual(G.parse('miner', G.write(g)), g)

    def test_errors(self):
        with self.assertRaises(ValueError):
            G.parse('x', 'DESC en no name\n')
        with self.assertRaises(ValueError):
            G.parse('x', 'NAME en X\nNAME fr Y\n')
        with self.assertRaises(ValueError):
            G.parse('x', 'NAME en X\nCOLOUR red\n')

    def test_old_card_groups_read_as_warp(self):
        g = G.parse('x', 'NAME en X\nTAGS apps,warp1260,warp1240,warp1260x,warp7060,ppc\n')
        self.assertEqual(g['tags'], ['apps', 'warp', 'ppc'])
        g = G.parse('x', 'NAME en X\nTAGS warp,warp_arm,warp7060\n')
        self.assertEqual(g['tags'], ['warp', 'warp_arm'])

    def test_catalog_tags_keep_old_buttons(self):
        self.assertEqual(G.catalog_tags(['apps', 'warp', 'warp_arm']),
                         ['apps', 'warp', 'warp_arm', 'warp1260', 'warp1240', 'warp1260x', 'warp7060'])
        self.assertEqual(G.catalog_tags(['warp_arm', 'ppc']), ['warp_arm', 'ppc'])
        self.assertEqual(G.catalog_tags([]), [])

    def test_catalog_files_use_new_tags(self):
        here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        for g in G.load_games(os.path.join(here, 'catalog', 'games')):
            for t in g['tags']:
                self.assertIn(t, G.TAGS, g['key'])

    def test_load_games_order_and_bad_name(self):
        with tempfile.TemporaryDirectory() as d:
            for key, order in (('b', 20), ('a', 30), ('c', 10)):
                with open(os.path.join(d, key + '.txt'), 'w', encoding='utf-8') as f:
                    f.write('NAME en %s\nORDER %d\n' % (key, order))
            self.assertEqual([g['key'] for g in G.load_games(d)], ['c', 'b', 'a'])
            with open(os.path.join(d, 'Bad Name.txt'), 'w') as f:
                f.write('NAME en x\n')
            with self.assertRaises(ValueError):
                G.load_games(d)


if __name__ == '__main__':
    unittest.main()
