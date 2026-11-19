#!/usr/bin/env python3
"""Is the release of a game repo ready for WARP HUB? Compares every file of the
newest releases/amiga/<Game>-<v>.zip with the same file in git (the index, i.e.
what a commit will send) and on disk.

    python tools/check_release.py <game repo drawer>

WARP HUB downloads the files one by one from git, so git must hold them byte
for byte as in the ZIP (.gitattributes: releases/** -text; files hidden by
.gitignore must be added with git add -f). Exit code 1 when anything differs.
"""
import glob
import os
import subprocess
import sys
import zipfile


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    os.chdir(sys.argv[1])
    zips = sorted(glob.glob('releases/amiga/*.zip'))
    if not zips:
        sys.exit('no releases/amiga/*.zip in %s' % sys.argv[1])
    z = zipfile.ZipFile(zips[-1])
    bad = 0
    names = [n for n in z.namelist() if not n.endswith('/')]
    for n in names:
        want = z.read(n)
        path = 'releases/amiga/' + n
        disk = open(path, 'rb').read() if os.path.exists(path) else None
        r = subprocess.run(['git', 'show', ':' + path], capture_output=True)
        git = r.stdout if r.returncode == 0 else None
        if disk != want or git != want:
            bad += 1
            print('%-60s disk %s, git %s' % (
                n, 'same' if disk == want else ('missing' if disk is None else 'DIFFERENT'),
                'same' if git == want else ('MISSING' if git is None else 'DIFFERENT')))
    attr = open('.gitattributes', encoding='utf-8').read() if os.path.exists('.gitattributes') else ''
    if 'releases/** -text' not in attr:
        print('.gitattributes has no "releases/** -text"')
        bad += 1
    print('%s: %d files, %s' % (zips[-1], len(names), 'all the same in git' if not bad else '%d problems' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
