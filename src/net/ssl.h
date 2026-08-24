#pragma once
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <stdint.h>

int net_init(void);
void net_shutdown(void);

SSL_CTX *ssl_ctx_new(void);

SSL *ssl_connect(SSL_CTX *ctx, const char *hostname, uint16_t port);

void ssl_disconnect(SSL *ssl);
