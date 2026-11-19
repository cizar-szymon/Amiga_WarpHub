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

/* Unix seconds for the certificate check instead of the clock of the Amiga. */
void hub_https_verify_time(ULONG unix_time);
void hub_https_cleanup(void);

HubConn *hub_conn_new(const char *host, int port);
void hub_conn_free(HubConn *c);

/* GET path (already URL-encoded) on c. Returns the HTTP status (200 ...) or
 * -1 on a network / TLS error (err says why). The body goes to sink.
 * location (if not NULL) gets the Location header of a redirect. */
int hub_get(HubConn *c, const char *path, HubSink sink, void *user,
            char *location, int loclen, char *err, int errlen);

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
