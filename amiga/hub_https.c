/* hub_https.c - HTTPS GET through AmiSSL 5 (see hub_https.h).
 *
 * After the AmiSSL SDK example Examples/https.c: OpenAmiSSLTags with the
 * socket base of this program, the certificates of AmiSSL:Certs, SNI and the
 * check of the host name (SSL_set1_host). HTTP/1.1 with keep-alive, bodies
 * by Content-Length, chunked, or up to the end of the connection. */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hub_mem.h"
#include "hub_https.h"

struct Library *SocketBase = NULL;
struct Library *AmiSSLMasterBase = NULL;
struct Library *AmiSSLBase = NULL;
struct Library *AmiSSLExtBase = NULL;

volatile int hub_break = 0;

static SSL_CTX *ctx = NULL;
static int ssl_open = 0;

#define IN_SIZE 32768
#define TIMEOUT_S 30

struct HubConn {
  char host[128];
  int port;
  long sock;
  SSL *ssl;
  int used;               /* requests on this connection */
  UBYTE *in;
  LONG in_pos, in_len;
  char cipher[80];
  int pinned;             /* the private source: own certificate */
  char pin[65];           /* its SHA-256 ("" = not known yet) */
  char peer[65];          /* SHA-256 of the certificate of this connection */
  char auth[120];         /* base64 of "warphub:<password>" or "" */
};

static void set_err(char *err, int errlen, const char *what)
{
  unsigned long e = ERR_get_error();
  if (e != 0) {
    char buf[160];
    ERR_error_string_n(e, buf, sizeof(buf));
    snprintf(err, errlen, "%s (%s)", what, buf);
  } else {
    snprintf(err, errlen, "%s", what);
  }
}

/* Extra seed only: AmiSSL 5 (OpenSSL 3) seeds itself ("os-specific"). */
static void seed_random(void)
{
  UWORD buf[64];
  struct DateStamp ds;
  int i;
  ULONG x;
  DateStamp(&ds);
  x = (ULONG)ds.ds_Tick ^ ((ULONG)ds.ds_Minute << 12) ^ (ULONG)FindTask(NULL);
  for (i = 0; i < 64; i++) {
    x = x * 1103515245UL + 12345UL;
    buf[i] = (UWORD)(x >> 16);
  }
  RAND_seed(buf, sizeof(buf));
}

int hub_https_init(const char *cafile, char *err, int errlen)
{
  SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
  if (SocketBase == NULL) {
    snprintf(err, errlen, "No TCP/IP stack (bsdsocket.library 4).");
    return 0;
  }
  AmiSSLMasterBase = OpenLibrary((CONST_STRPTR)"amisslmaster.library",
                                 AMISSLMASTER_MIN_VERSION);
  if (AmiSSLMasterBase == NULL) {
    snprintf(err, errlen, "AmiSSL 5 is needed (amisslmaster.library %d+); "
             "on Warp cards WarpSSL speeds it up.", AMISSLMASTER_MIN_VERSION);
    hub_https_cleanup();
    return 0;
  }
  if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                     AmiSSL_UsesOpenSSLStructs, FALSE,
                     AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                     AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                     AmiSSL_SocketBase, (ULONG)SocketBase,
                     AmiSSL_ErrNoPtr, (ULONG)&errno,
                     TAG_DONE) != 0) {
    snprintf(err, errlen, "AmiSSL could not be opened (too old?).");
    hub_https_cleanup();
    return 0;
  }
  ssl_open = 1;
  OPENSSL_init_ssl(OPENSSL_INIT_SSL_DEFAULT, NULL);
  seed_random();
  ctx = SSL_CTX_new(TLS_client_method());
  if (ctx == NULL) {
    set_err(err, errlen, "SSL_CTX_new");
    hub_https_cleanup();
    return 0;
  }
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  SSL_CTX_set_default_verify_paths(ctx);
  if (cafile != NULL && cafile[0] != 0 &&
      !SSL_CTX_load_verify_locations(ctx, cafile, NULL)) {
    set_err(err, errlen, "CAFILE unreadable");
    hub_https_cleanup();
    return 0;
  }
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
  return 1;
}

void hub_https_verify_time(ULONG unix_time)
{
  X509_VERIFY_PARAM_set_time(SSL_CTX_get0_param(ctx), (time_t)unix_time);
}

void hub_https_cleanup(void)
{
  if (ctx != NULL) {
    SSL_CTX_free(ctx);
    ctx = NULL;
  }
  if (ssl_open) {
    CloseAmiSSL();
    ssl_open = 0;
  }
  AmiSSLBase = AmiSSLExtBase = NULL;
  if (AmiSSLMasterBase != NULL) {
    CloseLibrary(AmiSSLMasterBase);
    AmiSSLMasterBase = NULL;
  }
  if (SocketBase != NULL) {
    CloseLibrary(SocketBase);
    SocketBase = NULL;
  }
}

/* --- the connection ------------------------------------------------------ */

HubConn *hub_conn_new(const char *host, int port)
{
  HubConn *c = (HubConn *)hub_calloc(1, sizeof(HubConn));
  if (c == NULL) return NULL;
  c->in = (UBYTE *)hub_malloc(IN_SIZE);
  if (c->in == NULL) {
    hub_free(c);
    return NULL;
  }
  strncpy(c->host, host, sizeof(c->host) - 1);
  c->port = port;
  c->sock = -1;
  return c;
}

void hub_conn_pin(HubConn *c, const char *pin)
{
  c->pinned = 1;
  strncpy(c->pin, pin, sizeof(c->pin) - 1);
  c->pin[sizeof(c->pin) - 1] = 0;
}

const char *hub_conn_peer(HubConn *c)
{
  return c->peer;
}

void hub_conn_auth(HubConn *c, const char *password)
{
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char plain[90];
  int n, i, o = 0;
  n = snprintf(plain, sizeof(plain), "warphub:%s", password);
  if (n >= (int)sizeof(plain)) n = sizeof(plain) - 1;
  for (i = 0; i < n && o < (int)sizeof(c->auth) - 5; i += 3) {
    ULONG v = (ULONG)(UBYTE)plain[i] << 16;
    if (i + 1 < n) v |= (ULONG)(UBYTE)plain[i + 1] << 8;
    if (i + 2 < n) v |= (UBYTE)plain[i + 2];
    c->auth[o++] = b64[(v >> 18) & 63];
    c->auth[o++] = b64[(v >> 12) & 63];
    c->auth[o++] = i + 1 < n ? b64[(v >> 6) & 63] : '=';
    c->auth[o++] = i + 2 < n ? b64[v & 63] : '=';
  }
  c->auth[o] = 0;
  memset(plain, 0, sizeof(plain));
}

/* SHA-256 of the certificate of the server, as 64 hex digits */
static int peer_sha(SSL *ssl, char hex[65])
{
  static const char digits[] = "0123456789abcdef";
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int n = 0, i;
  X509 *x = SSL_get1_peer_certificate(ssl);
  hex[0] = 0;
  if (x == NULL) return 0;
  if (!X509_digest(x, EVP_sha256(), md, &n) || n != 32) n = 0;
  X509_free(x);
  for (i = 0; i < n; i++) {
    hex[i * 2] = digits[md[i] >> 4];
    hex[i * 2 + 1] = digits[md[i] & 15];
  }
  hex[n * 2] = 0;
  return n == 32;
}

static void conn_close(HubConn *c)
{
  if (c->ssl != NULL) {
    SSL_shutdown(c->ssl);
    SSL_free(c->ssl);
    c->ssl = NULL;
  }
  if (c->sock >= 0) {
    CloseSocket(c->sock);
    c->sock = -1;
  }
  c->in_pos = c->in_len = 0;
  c->used = 0;
}

void hub_conn_free(HubConn *c)
{
  if (c == NULL) return;
  conn_close(c);
  memset(c->auth, 0, sizeof(c->auth));
  hub_free(c->in);
  hub_free(c);
}

static char last_cipher[80];

const char *hub_conn_cipher(HubConn *c)
{
  return c != NULL ? c->cipher : last_cipher;
}

static void check_break(void)
{
  if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) hub_break = 1;
}

static int conn_open(HubConn *c, char *err, int errlen)
{
  struct hostent *he;
  struct sockaddr_in addr;
  struct timeval tv;
  long one = 1;
  int r;

  he = gethostbyname((STRPTR)c->host);
  if (he == NULL) {
    snprintf(err, errlen, "Host %s not found.", c->host);
    return 0;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(c->port);
  addr.sin_len = sizeof(addr);
  memcpy(&addr.sin_addr, he->h_addr, he->h_length);
  c->sock = socket(AF_INET, SOCK_STREAM, 0);
  if (c->sock < 0) {
    snprintf(err, errlen, "No socket.");
    return 0;
  }
  tv.tv_secs = TIMEOUT_S;
  tv.tv_micro = 0;
  setsockopt(c->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(c->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(c->sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  if (connect(c->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    if (Errno() == EINTR) hub_break = 1;
    snprintf(err, errlen, "No connection to %s:%d (errno %ld).", c->host,
             c->port, (long)Errno());
    conn_close(c);
    return 0;
  }
  c->ssl = SSL_new(ctx);
  if (c->ssl == NULL) {
    set_err(err, errlen, "SSL_new");
    conn_close(c);
    return 0;
  }
  SSL_set_fd(c->ssl, c->sock);
  if (c->pinned) {
    /* own certificate: checked below by its SHA-256 (no date, no chain) */
    SSL_set_verify(c->ssl, SSL_VERIFY_NONE, NULL);
  } else {
    SSL_set_tlsext_host_name(c->ssl, c->host);
    SSL_set1_host(c->ssl, c->host);
  }
  r = SSL_connect(c->ssl);
  if (r != 1) {
    long v = SSL_get_verify_result(c->ssl);
    if (v != X509_V_OK) {
      snprintf(err, errlen, "Certificate of %s refused: %s.", c->host,
               X509_verify_cert_error_string(v));
    } else {
      set_err(err, errlen, "TLS handshake failed");
    }
    conn_close(c);
    return 0;
  }
  if (c->pinned) {
    if (!peer_sha(c->ssl, c->peer)) {
      snprintf(err, errlen, "No certificate from %s.", c->host);
      conn_close(c);
      return 0;
    }
    if (c->pin[0] && strcmp(c->pin, c->peer) != 0) {
      snprintf(err, errlen, "The certificate of %s is not the remembered one "
               "(%.16s): not connected.", c->host, c->peer);
      conn_close(c);
      return 0;
    }
  }
  snprintf(c->cipher, sizeof(c->cipher), "%s %s", SSL_get_version(c->ssl),
           SSL_CIPHER_get_name(SSL_get_current_cipher(c->ssl)));
  strcpy(last_cipher, c->cipher);
  return 1;
}

/* More bytes into c->in; 0 at the end of the connection or an error. */
static int fill(HubConn *c)
{
  int n;
  check_break();
  if (hub_break) return 0;
  if (c->in_pos > 0 && c->in_pos == c->in_len) c->in_pos = c->in_len = 0;
  if (c->in_len == IN_SIZE) {
    memmove(c->in, c->in + c->in_pos, c->in_len - c->in_pos);
    c->in_len -= c->in_pos;
    c->in_pos = 0;
  }
  n = SSL_read(c->ssl, c->in + c->in_len, IN_SIZE - c->in_len);
  if (n <= 0) {
    if (Errno() == EINTR) hub_break = 1;
    return 0;
  }
  c->in_len += n;
  return 1;
}

/* One header line without CR LF into line; 0 on an error. */
static int read_line(HubConn *c, char *line, int size)
{
  int len = 0;
  for (;;) {
    while (c->in_pos < c->in_len) {
      char ch = (char)c->in[c->in_pos++];
      if (ch == '\n') {
        if (len > 0 && line[len - 1] == '\r') len--;
        line[len] = 0;
        return 1;
      }
      if (len < size - 1) line[len++] = ch;
    }
    if (!fill(c)) return 0;
  }
}

/* n bytes of body (n < 0: up to the end) to sink. */
static int body(HubConn *c, LONG n, HubSink sink, void *user)
{
  while (n != 0) {
    LONG have = c->in_len - c->in_pos;
    if (have == 0) {
      if (!fill(c)) return n < 0;
      continue;
    }
    if (n > 0 && have > n) have = n;
    if (sink != NULL && !sink(user, c->in + c->in_pos, have)) return 0;
    c->in_pos += have;
    if (n > 0) n -= have;
  }
  return 1;
}

static int starts(const char *s, const char *with)
{
  while (*with) {
    char a = *s++, b = *with++;
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b) return 0;
  }
  return 1;
}

static const char *value_of(const char *line)
{
  const char *v = strchr(line, ':');
  if (v == NULL) return "";
  v++;
  while (*v == ' ' || *v == '\t') v++;
  return v;
}

static int request(HubConn *c, const char *path, HubSink sink, void *user,
                   char *location, int loclen, char *err, int errlen,
                   int *retry)
{
  char line[1024];
  int status, chunked = 0, close_after = 0, n, len;
  LONG length = -1;

  *retry = 0;
  if (c->ssl == NULL && !conn_open(c, err, errlen)) return -1;
  len = snprintf(line, sizeof(line),
                 "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: WarpHub/0.1 (Amiga)\r\n"
                 "Accept-Encoding: identity\r\nConnection: keep-alive\r\n%s%s%s\r\n",
                 path, c->host, c->auth[0] ? "Authorization: Basic " : "", c->auth,
                 c->auth[0] ? "\r\n" : "");
  if (len >= (int)sizeof(line)) {
    snprintf(err, errlen, "Path too long.");
    return -1;
  }
  n = SSL_write(c->ssl, line, len);
  if (n != len || !read_line(c, line, sizeof(line))) {
    /* a kept connection the server closed meanwhile: once more */
    *retry = c->used > 0 && !hub_break;
    snprintf(err, errlen, hub_break ? "Stopped (Ctrl-C)." : "Connection lost.");
    conn_close(c);
    return -1;
  }
  c->used++;
  if (strncmp(line, "HTTP/1.", 7) != 0 || strlen(line) < 12) {
    snprintf(err, errlen, "Not HTTP: %.60s", line);
    conn_close(c);
    return -1;
  }
  status = atoi(line + 9);
  if (strncmp(line, "HTTP/1.0", 8) == 0) close_after = 1;
  if (location != NULL && loclen > 0) location[0] = 0;
  for (;;) {
    if (!read_line(c, line, sizeof(line))) {
      snprintf(err, errlen, "Connection lost in the headers.");
      conn_close(c);
      return -1;
    }
    if (line[0] == 0) break;
    if (starts(line, "content-length:")) {
      length = atol(value_of(line));
    } else if (starts(line, "transfer-encoding:")) {
      chunked = starts(value_of(line), "chunked");
    } else if (starts(line, "connection:")) {
      if (starts(value_of(line), "close")) close_after = 1;
    } else if (starts(line, "location:") && location != NULL) {
      strncpy(location, value_of(line), loclen - 1);
      location[loclen - 1] = 0;
    }
  }
  /* only a 200 body goes to the sink, others are skipped */
  if (status != 200) sink = NULL;
  if (chunked) {
    for (;;) {
      LONG size;
      if (!read_line(c, line, sizeof(line))) break;
      size = strtol(line, NULL, 16);
      if (size == 0) {
        while (read_line(c, line, sizeof(line)) && line[0] != 0) {
        }
        break;
      }
      if (!body(c, size, sink, user) || !read_line(c, line, sizeof(line))) {
        snprintf(err, errlen, hub_break ? "Stopped (Ctrl-C)." : "Body cut off.");
        conn_close(c);
        return -1;
      }
    }
  } else if (length >= 0) {
    if (!body(c, length, sink, user)) {
      snprintf(err, errlen, hub_break ? "Stopped (Ctrl-C)." : "Body cut off.");
      conn_close(c);
      return -1;
    }
  } else {
    body(c, -1, sink, user);
    close_after = 1;
  }
  if (close_after) conn_close(c);
  return status;
}

int hub_get(HubConn *c, const char *path, HubSink sink, void *user,
            char *location, int loclen, char *err, int errlen)
{
  int retry, status;
  status = request(c, path, sink, user, location, loclen, err, errlen, &retry);
  if (status < 0 && retry) {
    status = request(c, path, sink, user, location, loclen, err, errlen, &retry);
  }
  return status;
}

void hub_url_path(char *out, int outlen, const char *path)
{
  static const char hex[] = "0123456789ABCDEF";
  int o = 0;
  for (; *path && o < outlen - 4; path++) {
    unsigned char ch = (unsigned char)*path;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~' || ch == '/') {
      out[o++] = (char)ch;
    } else {
      out[o++] = '%';
      out[o++] = hex[ch >> 4];
      out[o++] = hex[ch & 15];
    }
  }
  out[o] = 0;
}

/* --- SHA-256 ------------------------------------------------------------- */

struct HubSha {
  EVP_MD_CTX *md;
};

HubSha *hub_sha_new(void)
{
  HubSha *s = (HubSha *)hub_malloc(sizeof(HubSha));
  if (s == NULL) return NULL;
  s->md = EVP_MD_CTX_new();
  if (s->md == NULL || !EVP_DigestInit_ex(s->md, EVP_sha256(), NULL)) {
    if (s->md != NULL) EVP_MD_CTX_free(s->md);
    hub_free(s);
    return NULL;
  }
  return s;
}

void hub_sha_add(HubSha *s, const UBYTE *data, LONG len)
{
  EVP_DigestUpdate(s->md, data, len);
}

void hub_sha_end(HubSha *s, char hex[65])
{
  static const char digits[] = "0123456789abcdef";
  unsigned char out[32];
  unsigned int n = 0, i;
  EVP_DigestFinal_ex(s->md, out, &n);
  for (i = 0; i < 32; i++) {
    hex[i * 2] = digits[out[i] >> 4];
    hex[i * 2 + 1] = digits[out[i] & 15];
  }
  hex[64] = 0;
  EVP_MD_CTX_free(s->md);
  hub_free(s);
}
