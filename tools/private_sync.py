#!/usr/bin/env python3
"""Sends only the changed files of www/ (tools/make_private.py) to the private
server, swapped in one step like a whole send (tools/push_private.sh).

  python tools/private_sync.py user@host LOCAL_WWW [REMOTE_DIR] [--dry]
  --dry: only says what would be sent

1. The server's list of www/ (size, modification time; tar keeps the times,
   make_private.py leaves a file that did not change as it is).
2. On the server www.new = www with hard links (cp -al: quick, no space).
3. The new and changed files (size or time differ) as a tar stream into
   www.new, unpacked with --unlink-first: the old file in www/ stays as it
   was (a running download reads it to its end); the files not in LOCAL_WWW
   any more removed from www.new.
4. www.new -> www with one mv (www.old removed).
No www/ on the server yet: everything is sent.
"""
import os
import subprocess
import sys
import tarfile

SSH = ['ssh', '-o', 'BatchMode=yes']


def remote_list(host, d):
    """{path: (size, mtime)} of the server's www/ (empty when there is none)."""
    cmd = "cd %s/www 2>/dev/null && find . -type f -printf '%%s %%T@ %%P\\0' || true" % d
    out = subprocess.run(SSH + [host, cmd], check=True, capture_output=True).stdout
    files = {}
    for rec in out.split(b'\0'):
        if not rec:
            continue
        size, mtime, path = rec.split(b' ', 2)
        files[path.decode('utf-8')] = (int(size), int(float(mtime)))
    return files


def local_list(www):
    files = {}
    for base, _, names in os.walk(www):
        for n in names:
            full = os.path.join(base, n)
            rel = os.path.relpath(full, www).replace(os.sep, '/')
            st = os.stat(full)
            files[rel] = (st.st_size, int(st.st_mtime))
    return files


def run(host, cmd, data=None):
    subprocess.run(SSH + [host, cmd], check=True, input=data)


def main():
    dry = '--dry' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--dry']
    host, www = args[0], args[1]
    d = args[2] if len(args) > 2 else 'warphub-private'
    local = local_list(www)
    remote = remote_list(host, d)
    changed = sorted(p for p, v in local.items() if remote.get(p) != v)
    removed = sorted(p for p in remote if p not in local)
    size = sum(local[p][0] for p in changed)
    total = sum(v[0] for v in local.values())
    games = sorted(set(p.split('/')[1] for p in changed + removed if p.startswith('files/') and p.count('/') >= 2))
    print('%d files of %d changed (%.1f MB of %.1f MB), %d removed; games: %s' % (
        len(changed), len(local), size / 1e6, total / 1e6, len(removed), ' '.join(games) or '-'))
    if dry:
        for rel in changed[:20]:
            print('  changed', rel)
        for rel in removed[:20]:
            print('  removed', rel)
        return

    run(host, 'cd %s && rm -rf www.new && if [ -d www ]; then cp -al www www.new; else mkdir www.new; fi' % d)
    if changed:
        p = subprocess.Popen(SSH + [host, 'tar -C %s/www.new --unlink-first -xf -' % d], stdin=subprocess.PIPE)
        with tarfile.open(fileobj=p.stdin, mode='w|', format=tarfile.GNU_FORMAT) as tar:
            for i, rel in enumerate(changed, 1):
                info = tar.gettarinfo(os.path.join(www, *rel.split('/')), arcname=rel)
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                info.mode = 0o644
                with open(os.path.join(www, *rel.split('/')), 'rb') as f:
                    tar.addfile(info, f)
                if i % 50 == 0 or i == len(changed):
                    print('  sent %d of %d' % (i, len(changed)), flush=True)
        p.stdin.close()
        if p.wait() != 0:
            sys.exit('tar on the server failed')
    if removed:
        run(host, 'cd %s/www.new && xargs -0 rm -f --' % d, ('\0'.join(removed) + '\0').encode('utf-8'))
        run(host, 'cd %s/www.new && find . -type d -empty -delete' % d)
    run(host, 'cd %s && rm -rf www.old && { [ ! -d www ] || mv www www.old; } && mv www.new www && rm -rf www.old' % d)
    print('www/ swapped')


if __name__ == '__main__':
    main()
