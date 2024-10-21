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
  DISCRUB_ENOERR,
  DISCRUB_ENOMEM,
  DISCRUB_EARGS,
  DISCRUB_EHTTP,
  DISCRUB_EPARSE,
};

struct DiscordMessage {
  char *author_id;
  char *author_username;
  char *content;
  char *id;
  char *timestamp;
};

struct SearchOptions {
  char *author_id;
  char *channel_id;
  bool include_nsfw;
  size_t offset;
  char *content;
  char *mentions;
  bool pinned;
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

void discrub_free_search_response(struct SearchResponse *response);

#endif
