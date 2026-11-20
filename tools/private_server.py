#!/usr/bin/env python3
"""The private source of WARP HUB: an HTTPS server for the full packages
(tools/make_private.py), only for the Amigas of the owner.

    python tools/private_server.py setup [--root DIR]     certificate + password
    python tools/private_server.py run   [--root DIR] [--port 6831]
    python tools/private_server.py password [--root DIR]  a new password

<root> (default ../WarpHub-private, never in a repo):
    server.cfg        port, the salted hash of the password (PBKDF2-SHA256)
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


class State:
    def __init__(self, root, salt, digest):
        self.root = root
        self.www = os.path.realpath(os.path.join(root, 'www'))
        self.salt = salt
        self.digest = digest
        self.bad = {}            # ip -> (count, first time)
        self.blocked = {}        # ip -> until
        self.lock = threading.Lock()
        self.good = set()        # quick keys of passwords already checked
        self.log_path = os.path.join(root, 'server.log')

    def log(self, text):
        line = '%s %s' % (time.strftime('%Y-%m-%d %H:%M:%S'), text)
        print(line, flush=True)
        with self.lock, open(self.log_path, 'a', encoding='utf-8') as f:
            f.write(line + '\n')

    def password_ok(self, password):
        # PBKDF2 once (slow on purpose); then a quick key in memory, so the
        # hundreds of files of a game do not each cost the slow check
        quick = hashlib.sha256((self.salt + ':' + password).encode('utf-8')).digest()
        if quick in self.good:
            return True
        _, digest = hash_password(password, self.salt)
        if hmac.compare_digest(digest, self.digest):
            self.good.add(quick)
            return True
        return False


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
        if st.password_ok(password):
            with st.lock:
                was_bad = st.bad.pop(ip, None)
            if self.path == '/catalog.txt':
                st.log('login %s%s' % (ip, ' (after %d bad)' % was_bad[0] if was_bad else ''))
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

    def do_GET(self):
        if not self.authorized():
            return
        full = self.file_of()
        if full is None:
            self.reply(404, b'not found\n')
            return
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
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        Handler.state.log('stop')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('command', choices=('setup', 'run', 'password'))
    ap.add_argument('--root', default=default_root())
    ap.add_argument('--port', type=int, default=0)
    ap.add_argument('--password', help=argparse.SUPPRESS)   # tests only
    args = ap.parse_args(argv)
    if args.command == 'setup' and not args.port:
        args.port = 6831
    {'setup': cmd_setup, 'run': cmd_run, 'password': cmd_password}[args.command](args)


if __name__ == '__main__':
    main()
