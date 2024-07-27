#ifndef OPENSSL_HELPERS_H
#define OPENSSL_HELPERS_H

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct HTTPResponse {
  uint16_t code;
  char *data;
  /* use more generous size with larger applications */
  uint32_t length;
};

struct HTTPResponse *parse_response(const char* response, char **error);

char *send_request(BIO *bio, const char* request, char **error);

#endif
