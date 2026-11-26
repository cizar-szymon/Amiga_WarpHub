import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import private_sync as S  # noqa: E402

KEEP = ['files/web', 'images/web.png']


class Plan(unittest.TestCase):
    def test_without_keep_as_before(self):
        local = {'catalog.txt': (5, 2), 'files/a/A/x': (1, 1)}
        remote = {'catalog.txt': (4, 1), 'files/a/A/x': (1, 1), 'files/old/O/y': (1, 1)}
        self.assertEqual(S.plan(local, remote, []),
                         (['catalog.txt'], ['files/old/O/y'], [], []))

    def test_kept_paths_stay(self):
        local = {'catalog.txt': (5, 2), 'files/a/A/x': (1, 1)}
        remote = {'catalog.txt': (4, 1), 'files/a/A/x': (1, 1), 'files/web/W/z': (9, 9),
                  'images/web.png': (3, 3), 'files/webx/X/q': (1, 1)}
        changed, removed, refused, keep = S.plan(local, remote, KEEP)
        self.assertEqual(changed, ['catalog.txt'])
        self.assertEqual(removed, ['files/webx/X/q'])          # only the exact key is kept
        self.assertEqual(refused, [])
        self.assertEqual(keep, KEEP)

    def test_refused_and_take_over(self):
        local = {'files/web/W/z': (2, 2), 'images/web.png': (3, 4)}
        remote = {'files/web/W/z': (9, 9), 'files/web/W/old': (1, 1), 'images/web.png': (3, 3)}
        self.assertEqual(S.plan(local, remote, KEEP)[2], ['web'])
        changed, removed, refused, keep = S.plan(local, remote, KEEP, ['web'])
        self.assertEqual(refused, [])
        self.assertEqual(keep, [])
        self.assertEqual(changed, ['files/web/W/z', 'images/web.png'])
        self.assertEqual(removed, ['files/web/W/old'])

    def test_key_of(self):
        self.assertEqual(S.key_of('files/web'), 'web')
        self.assertEqual(S.key_of('images/web.png'), 'web')


if __name__ == '__main__':
    unittest.main()
