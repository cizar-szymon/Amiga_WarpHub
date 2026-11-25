/* hub_https - HTTPS GET through AmiSSL 5 (WarpSSL on Warp cards), with
 * keep-alive: one connection for many files of the same host. */
#ifndef HUB_HTTPS_H
#define HUB_HTTPS_H

#include <exec/types.h>

/* Called with every piece of a body; return 0 to stop (error). */
typedef int (*HubSink)(void *user, const UBYTE *data, LONG len);

typedef struct HubConn HubConn;

/* Opens bsdsocket.library and AmiSSL. cafile: an extra PEM file of trusted
 * certificates (tests), or NULL. Returns 0 and fills err. */
int hub_https_init(const char *cafile, char *err, int errlen);

/* The OpenSSL version of AmiSSL ("OpenSSL 3.5.4 30 Sep 2025"); after init. */
const char *hub_https_version(void);

/* Unix seconds for the certificate check instead of the clock of the Amiga. */
void hub_https_verify_time(ULONG unix_time);
void hub_https_cleanup(void);

HubConn *hub_conn_new(const char *host, int port);
void hub_conn_free(HubConn *c);

/* The private source (own certificate): c trusts only the certificate with
 * this SHA-256 (64 hex digits), not the chain nor the host name; pin "":
 * any certificate, its SHA-256 is then in hub_conn_peer (first login - ask
 * the user before a password goes over it). */
void hub_conn_pin(HubConn *c, const char *pin);
const char *hub_conn_peer(HubConn *c);
/* Every request of c with "Authorization: Basic" of this password. */
void hub_conn_auth(HubConn *c, const char *password);

/* GET path (already URL-encoded) on c. Returns the HTTP status (200 ...) or
 * -1 on a network / TLS error (err says why). The body goes to sink.
 * location (if not NULL) gets the Location header of a redirect. */
int hub_get(HubConn *c, const char *path, HubSink sink, void *user,
            char *location, int loclen, char *err, int errlen);

/* The next request of c only with "If-None-Match: etag": the answer is then
 * 304 (no body) when the file has not changed. */
void hub_conn_if_none_match(HubConn *c, const char *etag);
/* 1 when the last request of c failed in the network (lost, no data within
 * the time limit), not by an answer of the server: worth one more try. */
int hub_conn_net_error(HubConn *c);
/* The ETag of the last answer of c ("" = none). */
const char *hub_conn_etag(HubConn *c);

/* The protocol and cipher of c ("TLSv1.3 TLS_AES_128_GCM_SHA256"); c NULL:
 * of the last connection made. */
const char *hub_conn_cipher(HubConn *c);

/* URL-encodes a path: keeps A-Z a-z 0-9 - _ . ~ / and escapes the rest. */
void hub_url_path(char *out, int outlen, const char *path);

/* SHA-256 through AmiSSL. */
typedef struct HubSha HubSha;
HubSha *hub_sha_new(void);
void hub_sha_add(HubSha *s, const UBYTE *data, LONG len);
void hub_sha_end(HubSha *s, char hex[65]);   /* frees s */

/* Ctrl-C seen (set by the network code, checked by the caller). */
extern volatile int hub_break;

#endif
