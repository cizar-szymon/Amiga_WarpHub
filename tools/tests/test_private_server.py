"""tools/private_server.py: the main password and the codes (some games,
for a while) against a real server on a free port, in a temporary root."""
import base64
import io
import json
import os
import shutil
import socket
import ssl
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import private_server as ps  # noqa: E402

CATALOG = '''WARPHUB-CATALOG 1
# private
GAME alpha
NAME en Alpha
FILE 5 x N Alpha/a.txt
END
GAME beta
NAME en Beta
FILE 5 x N Beta/b.txt
END
'''


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


@unittest.skipUnless(shutil.which('openssl'), 'needs openssl (setup)')
class PrivateServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ps.ITERATIONS = 1000                       # fast in the test
        cls.root = tempfile.mkdtemp()
        www = os.path.join(cls.root, 'www')
        for key, name in (('alpha', 'Alpha/a.txt'), ('beta', 'Beta/b.txt')):
            path = os.path.join(www, 'files', key, *name.split('/'))
            os.makedirs(os.path.dirname(path))
            with open(path, 'w') as f:
                f.write(key)
        # a drawer next to www/: never served
        os.makedirs(os.path.join(cls.root, 'other'))
        with open(os.path.join(cls.root, 'other', 'secret.txt'), 'w') as f:
            f.write('NAME en Secret')
        with open(os.path.join(www, 'files', 'beta', 'Beta', 'map030.b&b'), 'w') as f:
            f.write('map')                          # '&' in a name (Syndicate Wars)
        os.makedirs(os.path.join(www, 'images'))
        for key in ('alpha', 'beta'):
            with open(os.path.join(www, 'images', key + '.png'), 'wb') as f:
                f.write(b'png')
        with open(os.path.join(www, 'catalog.txt'), 'w', encoding='utf-8', newline='') as f:
            f.write(CATALOG)
        cls.port = free_port()
        cls.cli('setup', '--port', str(cls.port), '--password', 'main-password-1')
        cls.code_alpha = cls.new_code('alpha-only', '--game', 'alpha')
        cls.code_day = cls.new_code('one-day', '--days', '1')
        # an expired one: made valid, then moved into the past
        cls.code_old = cls.new_code('old', '--hours', '1')
        data = ps.load_codes(cls.root)
        for c in data['codes']:
            if c['name'] == 'old':
                c['expires'] = int(time.time()) - 10
        ps.save_codes(cls.root, data)
        values = ps.load_cfg(cls.root)
        ps.Handler.state = ps.State(cls.root, values['salt'], values['hash'])
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(os.path.join(cls.root, 'cert.pem'), os.path.join(cls.root, 'key.pem'))
        cls.httpd = ps.Server(('127.0.0.1', cls.port), ps.Handler)
        cls.httpd.socket = ctx.wrap_socket(cls.httpd.socket, server_side=True,
                                           do_handshake_on_connect=False)
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        shutil.rmtree(cls.root, ignore_errors=True)

    @classmethod
    def cli(cls, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            ps.main(list(args) + ['--root', cls.root])
        return out.getvalue()

    @classmethod
    def new_code(cls, name, *args):
        out = cls.cli('code', name, *args)
        return out.split('code:')[1].split()[0]

    def get(self, path, password, etag=None, want_etag=False):
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        req = urllib.request.Request('https://127.0.0.1:%d%s' % (self.port, path))
        if password is not None:
            req.add_header('Authorization',
                           'Basic ' + base64.b64encode(('x:' + password).encode()).decode())
        if etag is not None:
            req.add_header('If-None-Match', etag)
        try:
            with urllib.request.urlopen(req, context=ctx, timeout=10) as r:
                out = r.status, r.read().decode()
                return out + (r.headers.get('ETag'),) if want_etag else out
        except urllib.error.HTTPError as e:
            out = e.code, e.read().decode()
            return out + (e.headers.get('ETag'),) if want_etag else out

    def post(self, path, password, data, length=None):
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        req = urllib.request.Request('https://127.0.0.1:%d%s' % (self.port, path), data=data, method='POST')
        if password is not None:
            req.add_header('Authorization',
                           'Basic ' + base64.b64encode(('x:' + password).encode()).decode())
        if length is not None:
            req.add_header('Content-Length', str(length))
        try:
            with urllib.request.urlopen(req, context=ctx, timeout=10) as r:
                return r.status, r.read().decode()
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode()

    def test_upload(self):
        data = bytes(1024 * 1024)
        self.assertEqual(self.post('/upload', 'main-password-1', data), (200, '1048576\n'))
        self.assertEqual(self.post('/upload', self.code_alpha, b'abc')[0], 200)   # any code
        self.assertEqual(self.post('/upload', None, b'abc')[0], 401)
        self.assertEqual(self.post('/catalog.txt', 'main-password-1', b'abc')[0], 404)

    def test_upload_limit(self):
        st = ps.Handler.state
        with st.lock:
            st.uploads = ps.UPLOAD_PARALLEL          # as if 3 were running
        try:
            self.assertEqual(self.post('/upload', 'main-password-1', b'abc')[0], 503)
        finally:
            with st.lock:
                st.uploads = 0
        self.assertEqual(self.post('/upload', 'main-password-1', b'abc')[0], 200)

    def test_catalog_etag_304(self):
        status, text, etag = self.get('/catalog.txt', 'main-password-1', want_etag=True)
        self.assertEqual(status, 200)
        self.assertTrue(etag and etag.startswith('"'))
        self.assertEqual(self.get('/catalog.txt', 'main-password-1', etag), (304, ''))
        self.assertEqual(self.get('/catalog.txt', 'main-password-1', '"other"')[0], 200)
        # a code for one game: its own catalog, another ETag
        status, text, etag2 = self.get('/catalog.txt', self.code_alpha, want_etag=True)
        self.assertEqual(status, 200)
        self.assertNotEqual(etag, etag2)
        self.assertEqual(self.get('/catalog.txt', self.code_alpha, etag)[0], 200)
        self.assertEqual(self.get('/catalog.txt', self.code_alpha, etag2)[0], 304)
        # no password: 401 even with the right ETag
        self.assertEqual(self.get('/catalog.txt', None, etag)[0], 401)

    def test_main_password_everything(self):
        status, text = self.get('/catalog.txt', 'main-password-1')
        self.assertEqual(status, 200)
        self.assertIn('GAME alpha', text)
        self.assertIn('GAME beta', text)
        self.assertEqual(self.get('/files/beta/Beta/b.txt', 'main-password-1'), (200, 'beta'))

    def test_outside_www_never_served(self):
        for path in ('/other/secret.txt', '/../other/secret.txt', '/%2e%2e/other/secret.txt',
                     '/files/beta/..%2f..%2f..%2fother/secret.txt', '/images/..%2f..%2fother%2fsecret.txt',
                     '/files/..%5c..%5cother/secret.txt', '/../server.cfg', '/..%2fserver.cfg'):
            self.assertNotEqual(self.get(path, 'main-password-1')[0], 200, path)

    def test_ampersand_in_a_name(self):
        # WARP HUB sends '&' as %26 (hub_url_path)
        self.assertEqual(self.get('/files/beta/Beta/map030.b%26b', 'main-password-1'), (200, 'map'))

    def test_code_for_one_game(self):
        status, text = self.get('/catalog.txt', self.code_alpha)
        self.assertEqual(status, 200)
        self.assertTrue(text.startswith('WARPHUB-CATALOG 1\n# private\nGAME alpha\n'))
        self.assertNotIn('beta', text)
        self.assertTrue(text.endswith('END\n'))
        self.assertEqual(self.get('/files/alpha/Alpha/a.txt', self.code_alpha), (200, 'alpha'))
        self.assertEqual(self.get('/images/alpha.png', self.code_alpha)[0], 200)
        self.assertEqual(self.get('/files/beta/Beta/b.txt', self.code_alpha)[0], 403)
        self.assertEqual(self.get('/images/beta.png', self.code_alpha)[0], 403)

    def test_code_for_a_day_everything(self):
        status, text = self.get('/catalog.txt', self.code_day)
        self.assertEqual((status, text), (200, CATALOG))
        self.assertEqual(self.get('/files/beta/Beta/b.txt', self.code_day), (200, 'beta'))

    def test_expired_code_refused_without_block(self):
        for _ in range(ps.MAX_BAD + 1):
            status, text = self.get('/catalog.txt', self.code_old)
            self.assertEqual(status, 401)
            self.assertIn('expired', text)
        self.assertEqual(self.get('/catalog.txt', 'main-password-1')[0], 200)   # not blocked

    def test_wrong_and_missing_password(self):
        self.assertEqual(self.get('/catalog.txt', None)[0], 401)
        self.assertEqual(self.get('/catalog.txt', 'nope-nope-nope')[0], 401)
        with ps.Handler.state.lock:
            ps.Handler.state.bad.clear()

    def test_revoke_takes_effect_without_restart(self):
        code = self.new_code('short-lived', '--game', 'beta')
        time.sleep(0.05)
        self.assertEqual(self.get('/files/beta/Beta/b.txt', code), (200, 'beta'))
        # a new mtime for sure (coarse clocks)
        self.cli('revoke', 'short-lived')
        path = ps.codes_path(self.root)
        st = os.stat(path)
        os.utime(path, ns=(st.st_atime_ns, st.st_mtime_ns + 10 ** 9))
        self.assertEqual(self.get('/files/beta/Beta/b.txt', code)[0], 401)
        with ps.Handler.state.lock:
            ps.Handler.state.bad.clear()

    def test_codes_file_has_no_plain_codes(self):
        with open(ps.codes_path(self.root), encoding='utf-8') as f:
            text = f.read()
        self.assertNotIn(self.code_alpha, text)
        self.assertNotIn(self.code_day, text)
        names = [c['name'] for c in json.loads(text)['codes']]
        self.assertIn('alpha-only', names)

    def test_code_needs_a_limit_and_a_known_game(self):
        with self.assertRaises(SystemExit):
            self.cli('code', 'full-forever')
        with self.assertRaises(SystemExit):
            self.cli('code', 'ghost', '--game', 'nosuchgame')


if __name__ == '__main__':
    unittest.main()
