import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import make_private as P  # noqa: E402


def write(d, name, text):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), 'w', encoding='utf-8') as f:
        f.write(text)


class MakePrivate(unittest.TestCase):
    def test_extra_descriptions(self):
        with tempfile.TemporaryDirectory() as d:
            pub, extra = os.path.join(d, 'pub'), os.path.join(d, 'extra')
            write(pub, 'alpha.txt', 'NAME en Alpha\nDESC en one\nORDER 10\n')
            write(extra, 'alpha.txt', 'NAME en Alpha\nDESC en two\nORDER 10\nLOCAL Alpha\n')
            write(extra, 'beta.txt', 'NAME en Beta\nORDER 5\nLOCAL Beta\n')
            games = P.all_games(pub, extra)
            self.assertEqual([g['key'] for g in games], ['beta', 'alpha'])
            self.assertEqual(games[1]['desc']['en'], 'two')
            self.assertEqual([g['key'] for g in P.all_games(pub, os.path.join(d, 'none'))], ['alpha'])

    def test_drop_old(self):
        with tempfile.TemporaryDirectory() as www:
            write(os.path.join(www, 'files', 'gone', 'Gone'), 'a', 'x')
            write(os.path.join(www, 'files', 'kept', 'Kept'), 'a', 'x')
            write(os.path.join(www, 'images'), 'gone.png', 'x')
            write(os.path.join(www, 'images'), 'kept.png', 'x')
            P.drop_old(www, {'kept'}, lambda s: None)
            self.assertEqual(os.listdir(os.path.join(www, 'files')), ['kept'])
            self.assertEqual(os.listdir(os.path.join(www, 'images')), ['kept.png'])


if __name__ == '__main__':
    unittest.main()
