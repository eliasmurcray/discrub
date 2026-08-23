#pragma once
#include "discord/discord.h"
#include <openssl/ssl.h>
#include <stdbool.h>

typedef enum {
    SCOPE_OK = 0,
    SCOPE_ERR_TRANSPORT,
    SCOPE_ERR_NOT_FOUND,
    SCOPE_ERR_AMBIGUOUS,
    SCOPE_ERR_INVALID_ARGS,
} ScopeStatus;

typedef struct {
    char *guild_id;
    char *guild_name;
    char *channel_id;
    char *channel_name;
    bool is_dm;
} ResolvedScope;

ScopeStatus scope_resolve_guild(SSL *ssl, const char *token, const char *query, char **out_id, char **out_name);
ScopeStatus scope_resolve_channel(SSL *ssl, const char *token, const char *guild_id, const char *query, char **out_id, char **out_name);
ScopeStatus scope_resolve_dm(SSL *ssl, const char *token, const char *query, char **out_id, char **out_name);

ScopeStatus scope_resolve(SSL *ssl, const char *token, const char *guild_query, const char *channel_query, const char *dm_query, ResolvedScope *out_scope);
void scope_free(ResolvedScope *scope);
