#!/usr/bin/env python3
"""The private source of WARP HUB: an HTTPS server for the full packages
(tools/make_private.py), only for the Amigas of the owner.

    python tools/private_server.py setup [--root DIR]     certificate + password
    python tools/private_server.py run   [--root DIR] [--port 6831]
    python tools/private_server.py password [--root DIR]  a new password

Extra passwords (codes) besides the main one, each with a name:
    code NAME [--game KEY ...] [--days N | --hours N] [--length N]
                                                        a new code, printed once
         --game: only these games (the catalog shows only them, other files 403)
         --days / --hours: valid until then (the server refuses it afterwards)
    codes                                               the list of the codes
    revoke NAME | revoke --expired                      remove codes
The codes are in codes.json (the hash only); the running server reads the
file again when it changes - no restart needed (tools/push_private.sh --codes).

<root> (default ../WarpHub-private, never in a repo):
    server.cfg        port, the salted hash of the password (PBKDF2-SHA256)
    codes.json        the codes: name, hash, games, valid until
    cert.pem key.pem  its own certificate (EC P-256, 20 years); WARP HUB
                      remembers its SHA-256 on the first login and from then
                      on talks only to a server with this certificate
    www/              catalog.txt, images/, files/<key>/... (make_private.py)
    server.log        every login, bad password and block

Every request needs the password (HTTP Basic, any user name) - only through
TLS. Without it: 401. Five bad passwords from one address: that address is
blocked for 15 minutes (403). Only GET of files under www/.

Python 3 standard library and the openssl command (setup only).
"""
import argparse
import base64
import configparser
import getpass
import hashlib
import hmac
import http.server
import json
import os
import secrets
import socketserver
import ssl
import subprocess
import sys
import threading
import time
import urllib.parse

ITERATIONS = 200000
MAX_BAD = 5
BLOCK_S = 15 * 60
CHUNK = 64 * 1024
# POST /upload (WARP NetMonitor: the upload speed to the private source): the
# body is read and thrown away; only with a password, at most this much a request
UPLOAD_MAX = 64 * 1024 * 1024
UPLOAD_PARALLEL = 3          # uploads at the same time; more: 503 at once
# run: called with (root, ssl context, State) after the start - a wrapper
# script on the server may add services of its own here
ON_START = []


def default_root():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(os.path.dirname(here), 'WarpHub-private')


def hash_password(password, salt=None):
    salt = salt or secrets.token_hex(16)
    digest = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), bytes.fromhex(salt),
                                 ITERATIONS).hex()
    return salt, digest


def fingerprint(cert_path):
    with open(cert_path, encoding='ascii') as f:
        der = ssl.PEM_cert_to_DER_cert(f.read())
    return hashlib.sha256(der).hexdigest()


def show_fingerprint(fp):
    groups = ' '.join(fp[i:i + 4].upper() for i in range(0, 16, 4))
    return '%s  (whole: %s)' % (groups, fp)


def load_cfg(root):
    cfg = configparser.ConfigParser()
    if not cfg.read(os.path.join(root, 'server.cfg')):
        sys.exit('no %s - run: private_server.py setup' % os.path.join(root, 'server.cfg'))
    return cfg['server']


def save_cfg(root, values):
    cfg = configparser.ConfigParser()
    cfg['server'] = values
    path = os.path.join(root, 'server.cfg')
    with open(path + '.tmp', 'w', encoding='utf-8') as f:
        cfg.write(f)
    os.replace(path + '.tmp', path)
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass


def ask_password():
    while True:
        a = getpass.getpass('New password (12+ characters): ')
        if len(a) < 12:
            print('too short')
            continue
        if ':' in a:
            print('no ":" please')
            continue
        if getpass.getpass('Once more: ') == a:
            return a
        print('not the same')


def cmd_setup(args):
    os.makedirs(os.path.join(args.root, 'www'), exist_ok=True)
    cert = os.path.join(args.root, 'cert.pem')
    key = os.path.join(args.root, 'key.pem')
    if not os.path.exists(cert):
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec',
                        '-pkeyopt', 'ec_paramgen_curve:prime256v1', '-nodes',
                        '-keyout', key, '-out', cert, '-days', '7300',
                        '-subj', '/CN=WARP HUB private'], check=True)
        try:
            os.chmod(key, 0o600)
        except OSError:
            pass
    password = args.password or ask_password()
    salt, digest = hash_password(password)
    save_cfg(args.root, {'port': str(args.port), 'salt': salt, 'hash': digest})
    print('certificate: %s' % show_fingerprint(fingerprint(cert)))
    print('WARP HUB shows the first 16 characters on the first login: they must match.')


def cmd_password(args):
    values = dict(load_cfg(args.root))
    values['salt'], values['hash'] = hash_password(args.password or ask_password())
    save_cfg(args.root, values)
    print('password changed (restart the server)')


# --- codes: extra passwords for some games and / or for a while ---------------
# One salt for all codes: a login costs one PBKDF2, not one per code (the codes
# are random, 8-12+ characters of 31 - no two people pick the same).

CODE_ALPHABET = 'abcdefghjkmnpqrstuvwxyz23456789'


def codes_path(root):
    return os.path.join(root, 'codes.json')


def load_codes(root):
    try:
        with open(codes_path(root), encoding='utf-8') as f:
            data = json.load(f)
    except FileNotFoundError:
        data = {}
    data.setdefault('salt', secrets.token_hex(16))
    data.setdefault('codes', [])
    return data


def save_codes(root, data):
    path = codes_path(root)
    with open(path + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)
    os.replace(path + '.tmp', path)
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass


def catalog_games(root):
    keys = []
    try:
        with open(os.path.join(root, 'www', 'catalog.txt'), encoding='utf-8') as f:
            for line in f:
                if line.startswith('GAME '):
                    keys.append(line.split()[1])
    except FileNotFoundError:
        pass
    return keys


def show_time(t):
    return time.strftime('%Y-%m-%d %H:%M', time.localtime(t))


def describe_code(c, now=None):
    now = now or time.time()
    games = ', '.join(c['games']) if c['games'] else 'all games'
    if not c['expires']:
        until = 'no time limit'
    elif c['expires'] <= now:
        until = 'EXPIRED %s' % show_time(c['expires'])
    else:
        until = 'until %s' % show_time(c['expires'])
    return '%-16s %-30s %s' % (c['name'], games, until)


def cmd_code(args):
    if not args.name or not args.name.replace('-', '').replace('_', '').isalnum():
        sys.exit('code NAME: letters, digits, - and _ (the name is in the log)')
    if not args.game and not args.days and not args.hours:
        sys.exit('a code is for some games (--game) and / or for a while (--days, --hours);'
                 ' full access without a limit is the main password')
    known = catalog_games(args.root)
    for g in args.game:
        if g not in known:
            sys.exit('no game %r in www/catalog.txt (there: %s)' % (g, ', '.join(known)))
    data = load_codes(args.root)
    if any(c['name'] == args.name for c in data['codes']):
        sys.exit('a code %r is there already (revoke %s first)' % (args.name, args.name))
    if args.length < 8:
        sys.exit('--length: 8 characters at least')
    code = args.password or ''.join(secrets.choice(CODE_ALPHABET) for _ in range(args.length))
    now = time.time()
    seconds = args.days * 86400 + args.hours * 3600
    entry = {'name': args.name, 'hash': hash_password(code, data['salt'])[1],
             'games': sorted(set(args.game)), 'expires': int(now + seconds) if seconds else 0,
             'created': int(now)}
    data['codes'].append(entry)
    save_codes(args.root, data)
    print('code:  %s' % code)
    print(describe_code(entry))
    print('Shown only now. To the server: sh tools/push_private.sh user@host --codes')


def cmd_codes(args):
    data = load_codes(args.root)
    if not data['codes']:
        print('no codes')
    for c in data['codes']:
        print(describe_code(c))


def cmd_revoke(args):
    data = load_codes(args.root)
    now = time.time()
    if args.expired:
        gone = [c for c in data['codes'] if c['expires'] and c['expires'] <= now]
    else:
        gone = [c for c in data['codes'] if c['name'] == args.name]
        if not gone:
            sys.exit('no code %r' % args.name)
    data['codes'] = [c for c in data['codes'] if c not in gone]
    save_codes(args.root, data)
    for c in gone:
        print('removed: %s' % describe_code(c, now))


def filter_catalog(text, games):
    """The catalog with only these games (the lines before the first GAME stay)."""
    out, keep = [], True
    for line in text.splitlines(keepends=True):
        if line.startswith('GAME '):
            keep = line.split()[1] in games
        if keep:
            out.append(line)
        if line.rstrip('\r\n') == 'END':
            keep = False             # until the next GAME decides
    return ''.join(out)


class State:
    def __init__(self, root, salt, digest):
        self.root = root
        self.www = os.path.realpath(os.path.join(root, 'www'))
        self.salt = salt
        self.digest = digest
        self.bad = {}            # ip -> (count, first time)
        self.blocked = {}        # ip -> until
        self.lock = threading.Lock()
        self.good = {}           # quick key of a password already checked -> who
        self.log_path = os.path.join(root, 'server.log')
        self.codes = {}          # hash -> code (codes.json)
        self.codes_salt = ''
        self.codes_mtime = None
        self.seen = set()        # (who, ip, game) already in the log
        self.uploads = 0         # POST /upload in progress

    def log(self, text):
        line = '%s %s' % (time.strftime('%Y-%m-%d %H:%M:%S'), text)
        print(line, flush=True)
        with self.lock, open(self.log_path, 'a', encoding='utf-8') as f:
            f.write(line + '\n')

    def reload_codes(self):
        # codes.json read again when it changed (a new or revoked code)
        try:
            mtime = os.stat(codes_path(self.root)).st_mtime_ns
        except FileNotFoundError:
            mtime = None
        with self.lock:
            if mtime == self.codes_mtime:
                return
            data = load_codes(self.root) if mtime is not None else {'salt': '', 'codes': []}
            self.codes = {c['hash']: c for c in data['codes']}
            self.codes_salt = data['salt']
            self.codes_mtime = mtime
            self.good = {q: w for q, w in self.good.items() if w == 'main'}
        self.log('codes: %d' % len(self.codes))

    def check(self, password):
        """None for a wrong password, else who it is: the main password (all
        games, no limit) or a code (its games, maybe expired)."""
        self.reload_codes()
        # PBKDF2 once (slow on purpose); then a quick key in memory, so the
        # hundreds of files of a game do not each cost the slow check
        quick = hashlib.sha256((self.salt + ':' + password).encode('utf-8')).digest()
        with self.lock:
            hit = self.good.get(quick)
        if hit is None:
            if hmac.compare_digest(hash_password(password, self.salt)[1], self.digest):
                hit = 'main'
            elif self.codes:
                digest = hash_password(password, self.codes_salt)[1]
                if digest in self.codes:
                    hit = digest
            if hit is None:
                return None
            with self.lock:
                self.good[quick] = hit
        if hit == 'main':
            return {'who': 'main', 'games': None, 'expired': False}
        code = self.codes.get(hit)
        if code is None:                     # revoked meanwhile
            return None
        return {'who': 'code ' + code['name'], 'games': set(code['games']) or None,
                'expired': bool(code['expires']) and code['expires'] <= time.time()}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    server_version = 'WarpHubPrivate/1'
    sys_version = ''
    timeout = 60
    state = None

    def log_message(self, fmt, *a):
        pass

    def setup(self):
        # the TLS handshake here, in the thread of this connection (with the
        # timeout), not in accept() where a slow client would stop the others
        self.request.settimeout(self.timeout)
        self.request.do_handshake()
        super().setup()

    def reply(self, code, text=b''):
        self.send_response(code)
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.send_header('Content-Length', str(len(text)))
        if code == 401:
            self.send_header('WWW-Authenticate', 'Basic realm="WARP HUB"')
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(text)

    def authorized(self):
        st = self.state
        ip = self.client_address[0]
        now = time.time()
        with st.lock:
            until = st.blocked.get(ip, 0)
            if until > now:
                blocked = True
            else:
                blocked = False
                st.blocked.pop(ip, None)
        if blocked:
            self.reply(403, b'blocked for a while\n')
            return False
        auth = self.headers.get('Authorization', '')
        password = None
        if auth.startswith('Basic '):
            try:
                password = base64.b64decode(auth[6:]).decode('utf-8').split(':', 1)[1]
            except (ValueError, IndexError, UnicodeDecodeError):
                password = None
        if password is None:
            self.reply(401, b'password needed\n')
            return False
        grant = st.check(password)
        if grant is not None and grant['expired']:
            # a real code, only too late: not a guess, no block
            if self.path == '/catalog.txt':
                st.log('expired %s %s' % (grant['who'], ip))
            self.reply(401, b'code expired\n')
            return False
        if grant is not None:
            self.grant = grant
            with st.lock:
                was_bad = st.bad.pop(ip, None)
            if self.path == '/catalog.txt':
                with st.lock:
                    st.seen = {s for s in st.seen if s[:2] != (grant['who'], ip)}
                st.log('login %s%s%s' % (ip, '' if grant['who'] == 'main' else ' ' + grant['who'],
                                         ' (after %d bad)' % was_bad[0] if was_bad else ''))
            return True
        with st.lock:
            count, first = st.bad.get(ip, (0, now))
            if now - first > BLOCK_S:
                count, first = 0, now
            count += 1
            st.bad[ip] = (count, first)
            if count >= MAX_BAD:
                st.blocked[ip] = now + BLOCK_S
                st.bad.pop(ip, None)
        st.log('bad password %s (%d)%s' % (ip, count,
                                           ' - blocked %d min' % (BLOCK_S // 60)
                                           if count >= MAX_BAD else ''))
        time.sleep(1)
        self.reply(401, b'wrong password\n')
        return False

    def file_of(self):
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        parts = [p for p in path.split('/') if p]
        if not parts or any(p in ('.', '..') or '\\' in p for p in parts):
            return None
        full = os.path.realpath(os.path.join(self.state.www, *parts))
        if not full.startswith(self.state.www + os.sep) or not os.path.isfile(full):
            return None
        return full

    def do_HEAD(self):
        self.do_GET()

    def game_of(self):
        """files/<key>/... and images/<key>.png belong to a game; None else."""
        parts = [p for p in urllib.parse.unquote(urllib.parse.urlsplit(self.path).path).split('/')
                 if p]
        if len(parts) >= 3 and parts[0] == 'files':
            return parts[1]
        if len(parts) == 2 and parts[0] == 'images':
            return os.path.splitext(parts[1])[0]
        return None

    def do_POST(self):
        """POST /upload: the speed of sending from the Amiga. The body is read
        and dropped; the answer is the number of bytes. Only with a valid
        password or code (any), at most UPLOAD_MAX a request."""
        if self.path.split('?', 1)[0] != '/upload':
            self.close_connection = True
            self.reply(404, b'not found\n')
            return
        if not self.authorized():
            self.close_connection = True       # the body is not read
            return
        try:
            n = int(self.headers.get('Content-Length', '-1'))
        except ValueError:
            n = -1
        if n < 0 or n > UPLOAD_MAX:
            self.close_connection = True
            self.reply(411 if n < 0 else 413, b'length needed\n' if n < 0 else b'too large\n')
            return
        st = self.state
        with st.lock:
            busy = st.uploads >= UPLOAD_PARALLEL
            if not busy:
                st.uploads += 1
        if busy:
            # a few test connections at most: the RP4 serves games first
            self.close_connection = True
            self.reply(503, b'busy, try later\n')
            return
        try:
            left = n
            while left > 0:
                block = self.rfile.read(min(CHUNK, left))
                if not block:
                    break
                left -= len(block)
        finally:
            with st.lock:
                st.uploads -= 1
        if left:
            self.close_connection = True
            return
        key = (self.grant['who'], self.client_address[0], 'upload')
        with st.lock:
            new = key not in st.seen
            st.seen.add(key)
        if new:
            st.log('upload %s%s' % (key[1], '' if key[0] == 'main' else ' ' + key[0]))
        self.reply(200, ('%d\n' % n).encode())

    def do_GET(self):
        if not self.authorized():
            return
        full = self.file_of()
        if full is None:
            self.reply(404, b'not found\n')
            return
        games = self.grant['games']
        game = self.game_of()
        if full == os.path.join(self.state.www, 'catalog.txt'):
            # the catalog (only the games of a code) with an ETag: WARP HUB
            # asks If-None-Match and gets 304 without it when it is the same
            with open(full, encoding='utf-8', newline='') as f:
                text = f.read()
            if games is not None:
                text = filter_catalog(text, games)
            body = text.encode('utf-8')
            etag = '"%s"' % hashlib.sha256(body).hexdigest()[:32]
            if self.headers.get('If-None-Match', '').strip() == etag:
                self.send_response(304)
                self.send_header('ETag', etag)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('ETag', etag)
            self.end_headers()
            if self.command != 'HEAD':
                self.wfile.write(body)
            return
        if games is not None and game not in games:
            self.reply(403, b'not for this code\n')
            return
        if game is not None and self.path.startswith('/files/'):
            st = self.state
            key = (self.grant['who'], self.client_address[0], game)
            with st.lock:
                new = key not in st.seen
                st.seen.add(key)
            if new:
                st.log('files %s %s%s' % (game, key[1],
                                          '' if key[0] == 'main' else ' ' + key[0]))
        size = os.path.getsize(full)
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(size))
        self.end_headers()
        if self.command == 'HEAD':
            return
        with open(full, 'rb') as f:
            for block in iter(lambda: f.read(CHUNK), b''):
                self.wfile.write(block)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        pass                     # broken handshakes, scanners: no traceback


def cmd_run(args):
    values = load_cfg(args.root)
    port = args.port or int(values.get('port', 6831))
    Handler.state = State(args.root, values['salt'], values['hash'])
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(os.path.join(args.root, 'cert.pem'), os.path.join(args.root, 'key.pem'))
    httpd = Server(('', port), Handler)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True,
                                   do_handshake_on_connect=False)
    Handler.state.log('start, port %d, certificate %s' % (
        port, show_fingerprint(fingerprint(os.path.join(args.root, 'cert.pem')))))
    for fn in ON_START:
        try:
            fn(args.root, ctx, Handler.state)
        except Exception as e:                       # noqa: BLE001 - the games go on
            Handler.state.log('start hook failed: %s' % e)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        Handler.state.log('stop')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('command', choices=('setup', 'run', 'password', 'code', 'codes', 'revoke'))
    ap.add_argument('name', nargs='?', help='code / revoke: the name of the code')
    ap.add_argument('--root', default=default_root())
    ap.add_argument('--port', type=int, default=0)
    ap.add_argument('--game', action='append', default=[], help='code: only this game (key)')
    ap.add_argument('--days', type=int, default=0, help='code: valid for N days')
    ap.add_argument('--hours', type=int, default=0, help='code: valid for N hours')
    ap.add_argument('--length', type=int, default=12, help='code: characters (8+, default 12)')
    ap.add_argument('--expired', action='store_true', help='revoke: all expired codes')
    ap.add_argument('--password', help=argparse.SUPPRESS)   # tests only
    args = ap.parse_args(argv)
    if args.command == 'setup' and not args.port:
        args.port = 6831
    if args.command == 'revoke' and not args.name and not args.expired:
        ap.error('revoke NAME or revoke --expired')
    {'setup': cmd_setup, 'run': cmd_run, 'password': cmd_password, 'code': cmd_code,
     'codes': cmd_codes, 'revoke': cmd_revoke}[args.command](args)


if __name__ == '__main__':
    main()
