#ifndef DISCRUB_INTERFACE_H
#define DISCRUB_INTERFACE_H

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int discrub_delete_message(BIO *bio, const char *guild_id, const char *message_id, char **error);

#endif
