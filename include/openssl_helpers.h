#ifndef OPENSSL_HELPERS_H
#define OPENSSL_HELPERS_H

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct HTTPResponse {
  uint16_t code;
  char *data;
  unsigned int length;
};

enum HTTPError {
  HTTP_ENOMEM,
  HTTP_EBIO,
  HTTP_EPARSE,
};

struct HTTPResponse *http_request(BIO *connection, const char *request,
                                  enum HTTPError *error);

const char *http_strerror(enum HTTPError *error);

#endif
