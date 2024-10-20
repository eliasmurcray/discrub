#ifndef DISCRUB_INTERFACE_H
#define DISCRUB_INTERFACE_H

#include <errno.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "jsontok.h"
#include "openssl_helpers.h"

enum DiscrubError {
  DISCRUB_ENOMEM,
  DISCRUB_EARGS,
  DISCRUB_EHTTP,
  DISCRUB_EPARSE,
};

struct DiscordMessage {
  const char *author_id;
  const char *author_username;
  const char *content;
  const char *id;
  const char *timestamp;
};

struct SearchOptions {
  const char *author_id;
  const char *channel_id;
  const bool include_nsfw;
  const size_t offset;
  const char *content;
  const char *mentions;
  const bool pinned;
};

struct SearchResponse {
  struct DiscordMessage *messages;
  size_t length;
};

bool discrub_delete_message(BIO *connection, const char *token,
                            const char *channel_id, const char *message_id,
                            enum DiscrubError *error);

struct SearchResponse *discrub_search(BIO *connection, const char *token,
                                      const char *server_id,
                                      struct SearchOptions *options,
                                      enum DiscrubError *error);

const char *discrub_strerror(enum DiscrubError *error);

#endif
