#ifndef DISCRUB_INTERFACE_H
#define DISCRUB_INTERFACE_H

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "openssl_helpers.h"                                                                                                                                                               
#include "json.h"

struct DiscordMessage {
  const char *author_id;
  const char* author_username;
  const char *content;
  const char *id;
  const char* timestamp;
};

struct SearchOpts {
  const char *author_id;
  const char *channel_id;
  const unsigned char include_nsfw;
  const unsigned int offset;
};

struct SearchResponse {
  struct DiscordMessage* messages;
  unsigned int length;
};

unsigned char discrub_delete_message(BIO *bio, const char* token, const char *channel_id, const char *message_id, char **error);

struct SearchResponse* discrub_search_messages(BIO *bio, const char *token, const char *server_id, struct SearchOpts *options, char **error);

#endif
