#include "scope.h"
#include "common/strutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *id;
    const char *name;
} scope_candidate_t;

static int is_snowflake(const char *s) {
    size_t len = strlen(s);
    if (len < 16 || len > 20) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }
    return 1;
}

static const char *strip_prefix(const char *query) {
    if (query[0] == '#' || query[0] == '@') {
        return query + 1;
    }
    return query;
}

static ScopeStatus take_candidate(const scope_candidate_t *candidate,
                                  char **out_id, char **out_name) {
    *out_id = dup_str(candidate->id);
    *out_name = dup_str(candidate->name);
    if (!*out_id || !*out_name) {
        free(*out_id);
        free(*out_name);
        *out_id = NULL;
        *out_name = NULL;
        return SCOPE_ERR_TRANSPORT;
    }
    return SCOPE_OK;
}

static ScopeStatus take_snowflake(const char *query, char **out_id,
                                  char **out_name) {
    *out_id = dup_str(query);
    *out_name = dup_str(query);
    if (!*out_id || !*out_name) {
        free(*out_id);
        free(*out_name);
        *out_id = NULL;
        *out_name = NULL;
        return SCOPE_ERR_TRANSPORT;
    }
    return SCOPE_OK;
}

static ScopeStatus match_candidates(const scope_candidate_t *candidates,
                                    size_t count, const char *raw_query,
                                    const char *kind_label, char **out_id,
                                    char **out_name) {
    const char *query = strip_prefix(raw_query);
    for (size_t i = 0; i < count; i++) {
        if (candidates[i].name && str_eq_ci(candidates[i].name, query)) {
            return take_candidate(&candidates[i], out_id, out_name);
        }
    }
    size_t match_count = 0;
    size_t match_index = 0;
    for (size_t i = 0; i < count; i++) {
        if (candidates[i].name && str_contains_ci(candidates[i].name, query)) {
            match_count++;
            match_index = i;
        }
    }
    if (match_count == 1) {
        return take_candidate(&candidates[match_index], out_id, out_name);
    }
    if (match_count == 0) {
        fprintf(stderr, "no %s matching \"%s\"\n", kind_label, raw_query);
        return SCOPE_ERR_NOT_FOUND;
    }
    fprintf(stderr, "multiple %s match \"%s\":\n", kind_label, raw_query);
    for (size_t i = 0; i < count; i++) {
        if (candidates[i].name && str_contains_ci(candidates[i].name, query)) {
            fprintf(stderr, "  %s (%s)\n", candidates[i].name,
                    candidates[i].id);
        }
    }
    return SCOPE_ERR_AMBIGUOUS;
}

static ScopeStatus match_in_channel_list(const DiscordChannelList *channels,
                                         const char *query,
                                         const char *kind_label, char **out_id,
                                         char **out_name) {
    if (channels->count == 0) {
        return match_candidates(NULL, 0, query, kind_label, out_id, out_name);
    }
    scope_candidate_t *candidates =
        malloc(channels->count * sizeof(scope_candidate_t));
    if (!candidates) {
        return SCOPE_ERR_TRANSPORT;
    }
    for (size_t i = 0; i < channels->count; i++) {
        candidates[i].id = channels->items[i].id;
        candidates[i].name = channels->items[i].name;
    }
    ScopeStatus result = match_candidates(candidates, channels->count, query,
                                          kind_label, out_id, out_name);
    free(candidates);
    return result;
}

ScopeStatus scope_resolve_guild(SSL *ssl, const char *token, const char *query,
                                char **out_id, char **out_name) {
    if (!ssl || !token || !query || !out_id || !out_name) {
        return SCOPE_ERR_INVALID_ARGS;
    }
    *out_id = NULL;
    *out_name = NULL;
    if (is_snowflake(query)) {
        return take_snowflake(query, out_id, out_name);
    }
    DiscordGuildList guilds;
    DiscordStatus status = discord_get_guilds(ssl, token, &guilds);
    if (status != DISCORD_OK) {
        fprintf(stderr, "failed to list guilds\n");
        return SCOPE_ERR_TRANSPORT;
    }
    ScopeStatus result;
    if (guilds.count == 0) {
        result = match_candidates(NULL, 0, query, "guilds", out_id, out_name);
    } else {
        scope_candidate_t *candidates =
            malloc(guilds.count * sizeof(scope_candidate_t));
        if (!candidates) {
            discord_guild_list_free(&guilds);
            return SCOPE_ERR_TRANSPORT;
        }
        for (size_t i = 0; i < guilds.count; i++) {
            candidates[i].id = guilds.items[i].id;
            candidates[i].name = guilds.items[i].name;
        }
        result = match_candidates(candidates, guilds.count, query, "guilds",
                                  out_id, out_name);
        free(candidates);
    }
    discord_guild_list_free(&guilds);
    return result;
}

ScopeStatus scope_resolve_channel(SSL *ssl, const char *token,
                                  const char *guild_id, const char *query,
                                  char **out_id, char **out_name) {
    if (!ssl || !token || !guild_id || !query || !out_id || !out_name) {
        return SCOPE_ERR_INVALID_ARGS;
    }
    *out_id = NULL;
    *out_name = NULL;
    if (is_snowflake(query)) {
        return take_snowflake(query, out_id, out_name);
    }
    DiscordChannelList channels;
    DiscordStatus status =
        discord_get_guild_channels(ssl, token, guild_id, &channels);
    if (status != DISCORD_OK) {
        fprintf(stderr, "failed to list channels\n");
        return SCOPE_ERR_TRANSPORT;
    }
    ScopeStatus result =
        match_in_channel_list(&channels, query, "channels", out_id, out_name);
    discord_channel_list_free(&channels);
    return result;
}

ScopeStatus scope_resolve_dm(SSL *ssl, const char *token, const char *query,
                             char **out_id, char **out_name) {
    if (!ssl || !token || !query || !out_id || !out_name) {
        return SCOPE_ERR_INVALID_ARGS;
    }
    *out_id = NULL;
    *out_name = NULL;
    if (is_snowflake(query)) {
        return take_snowflake(query, out_id, out_name);
    }
    DiscordChannelList channels;
    DiscordStatus status = discord_get_dm_channels(ssl, token, &channels);
    if (status != DISCORD_OK) {
        fprintf(stderr, "failed to list dms\n");
        return SCOPE_ERR_TRANSPORT;
    }
    ScopeStatus result =
        match_in_channel_list(&channels, query, "dms", out_id, out_name);
    discord_channel_list_free(&channels);
    return result;
}

ScopeStatus scope_resolve(SSL *ssl, const char *token, const char *guild_query,
                          const char *channel_query, const char *dm_query,
                          ResolvedScope *out_scope) {
    if (!ssl || !token || !out_scope) {
        return SCOPE_ERR_INVALID_ARGS;
    }
    memset(out_scope, 0, sizeof(*out_scope));
    if (dm_query) {
        if (guild_query || channel_query) {
            fprintf(stderr,
                    "--dm cannot be combined with --guild or --channel\n");
            return SCOPE_ERR_INVALID_ARGS;
        }
        out_scope->is_dm = true;
        return scope_resolve_dm(ssl, token, dm_query, &out_scope->channel_id,
                                &out_scope->channel_name);
    }
    if (!guild_query) {
        if (channel_query) {
            fprintf(stderr, "--channel requires --guild\n");
            return SCOPE_ERR_INVALID_ARGS;
        }
        fprintf(stderr, "specify --guild, --guild with --channel, or --dm\n");
        return SCOPE_ERR_INVALID_ARGS;
    }
    ScopeStatus status = scope_resolve_guild(
        ssl, token, guild_query, &out_scope->guild_id, &out_scope->guild_name);
    if (status != SCOPE_OK) {
        return status;
    }
    if (channel_query) {
        status = scope_resolve_channel(ssl, token, out_scope->guild_id,
                                       channel_query, &out_scope->channel_id,
                                       &out_scope->channel_name);
        if (status != SCOPE_OK) {
            free(out_scope->guild_id);
            free(out_scope->guild_name);
            out_scope->guild_id = NULL;
            out_scope->guild_name = NULL;
            return status;
        }
    }
    return SCOPE_OK;
}

void scope_free(ResolvedScope *scope) {
    if (!scope) {
        return;
    }
    free(scope->guild_id);
    free(scope->guild_name);
    free(scope->channel_id);
    free(scope->channel_name);
    memset(scope, 0, sizeof(*scope));
}
