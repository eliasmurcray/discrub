#include "cli.h"
#include "common/timeutil.h"
#include "discord/discord.h"
#include "filters.h"
#include "scope.h"
#include <cargs.h>
#include <sodium.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct cag_option options[] = {
    {.identifier = 'g',
     .access_letters = "g",
     .access_name = "guild",
     .value_name = "NAME",
     .description = "Target guild (name or id)"},
    {.identifier = 'c',
     .access_letters = "c",
     .access_name = "channel",
     .value_name = "NAME",
     .description = "Target channel within --guild"},
    {.identifier = 'd',
     .access_letters = "d",
     .access_name = "dm",
     .value_name = "NAME",
     .description = "Target DM/group DM (name or id)"},
    {.identifier = 'C',
     .access_letters = NULL,
     .access_name = "content",
     .value_name = "TEXT",
     .description = "Only messages containing this text"},
    {.identifier = 'a',
     .access_letters = NULL,
     .access_name = "author",
     .value_name = "ID[,ID...]",
     .description = "Only messages from these numeric user id(s)"},
    {.identifier = 'm',
     .access_letters = NULL,
     .access_name = "me",
     .description = "Only messages from you (shortcut for --author=<your id>)"},
    {.identifier = 'H',
     .access_letters = NULL,
     .access_name = "has",
     .value_name = "TYPE",
     .description = "image|video|file|link|embed|sound"},
    {.identifier = 'o',
     .access_letters = NULL,
     .access_name = "older-than",
     .value_name = "DURATION",
     .description = "e.g. 30d, 12h, 90m"},
    {.identifier = 'n',
     .access_letters = NULL,
     .access_name = "newer-than",
     .value_name = "DURATION",
     .description = "e.g. 7d, 24h"},
    {.identifier = 'l',
     .access_letters = NULL,
     .access_name = "limit",
     .value_name = "N",
     .description = "Process at most N matching messages"},
    {.identifier = 'y',
     .access_letters = "y",
     .access_name = "confirm",
     .description = "Actually delete (default: preview only)"},
    {.identifier = 'h',
     .access_letters = "h",
     .access_name = "help",
     .description = "Show this help"},
};

static DiscordStatus search_with_retry(SSL *ssl, const char *token,
                                       const ResolvedScope *scope,
                                       const DiscordSearchParams *params,
                                       DiscordMessageList *out_list) {
    for (int attempt = 0; attempt < 15; attempt++) {
        int retry_after_ms = 0;
        DiscordStatus status;
        if (scope->is_dm) {
            status = discord_search_channel_messages(ssl, token,
                                                     scope->channel_id, params,
                                                     out_list, &retry_after_ms);
        } else if (scope->channel_id) {
            DiscordSearchParams scoped_params = *params;
            scoped_params.channel_id = scope->channel_id;
            status = discord_search_guild_messages(ssl, token, scope->guild_id,
                                                   &scoped_params, out_list,
                                                   &retry_after_ms);
        } else {
            status = discord_search_guild_messages(
                ssl, token, scope->guild_id, params, out_list, &retry_after_ms);
        }
        if (status == DISCORD_ERR_NOT_INDEXED) {
            printf("waiting for indexing to finish...\n");
            sleep_ms(retry_after_ms > 0 ? retry_after_ms : 2000);
            continue;
        }
        if (status == DISCORD_ERR_RATE_LIMITED) {
            continue;
        }
        return status;
    }
    fprintf(stderr, "gave up waiting for search to become available\n");
    return DISCORD_ERR_TRANSPORT;
}

static DiscordStatus run_search(SSL *ssl, const char *token,
                                const ResolvedScope *scope, const char *content,
                                const char *author_id, const char *has,
                                const char *older_than, const char *newer_than,
                                DiscordMessageList *out_list) {
    BuiltFilters filters;
    if (!filters_build(content, author_id, has, older_than, newer_than, 0,
                       &filters)) {
        return DISCORD_ERR_JSON;
    }
    DiscordStatus status =
        search_with_retry(ssl, token, scope, &filters.params, out_list);
    filters_free(&filters);
    return status;
}

static int delete_with_retry(SSL *ssl, const char *token,
                             const char *channel_id, const char *message_id) {
    for (int attempt = 0; attempt < 5; attempt++) {
        DiscordStatus status =
            discord_delete_message(ssl, token, channel_id, message_id);
        if (status == DISCORD_OK) {
            return 1;
        }
        if (status != DISCORD_ERR_RATE_LIMITED) {
            return 0;
        }
    }
    return 0;
}

int cmd_purge(SSL *ssl, int argc, char **argv) {
    const char *guild_arg = NULL;
    const char *channel_arg = NULL;
    const char *dm_arg = NULL;
    const char *content_arg = NULL;
    const char *author_arg = NULL;
    const char *has_arg = NULL;
    const char *older_than_arg = NULL;
    const char *newer_than_arg = NULL;
    long limit = 0;
    bool confirm = false;
    bool me = false;
    cag_option_context context;
    cag_option_init(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
    while (cag_option_fetch(&context)) {
        const char *value;
        switch (cag_option_get_identifier(&context)) {
        case 'g':
            guild_arg = cag_option_get_value(&context);
            break;
        case 'c':
            channel_arg = cag_option_get_value(&context);
            break;
        case 'd':
            dm_arg = cag_option_get_value(&context);
            break;
        case 'C':
            content_arg = cag_option_get_value(&context);
            break;
        case 'a':
            author_arg = cag_option_get_value(&context);
            break;
        case 'm':
            me = true;
            break;
        case 'H':
            has_arg = cag_option_get_value(&context);
            break;
        case 'o':
            older_than_arg = cag_option_get_value(&context);
            break;
        case 'n':
            newer_than_arg = cag_option_get_value(&context);
            break;
        case 'l':
            value = cag_option_get_value(&context);
            limit = value ? strtol(value, NULL, 10) : 0;
            break;
        case 'y':
            confirm = true;
            break;
        case 'h':
            printf("Usage: xcord purge [OPTION]...\n");
            printf("Search and delete messages matching filters. Without "
                   "--confirm, only previews.\n");
            printf("Matches messages from anyone by default; use --me or "
                   "--author to narrow by sender.\n\n");
            cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
            return 0;
        case '?':
            cag_option_print_error(&context, stdout);
            return 1;
        }
    }
    if (author_arg && me) {
        fprintf(stderr, "--author and --me cannot be used together\n");
        return 1;
    }
    char *token = cli_load_token();
    if (!token) {
        return 1;
    }
    ResolvedScope scope;
    ScopeStatus scope_status =
        scope_resolve(ssl, token, guild_arg, channel_arg, dm_arg, &scope);
    if (scope_status != SCOPE_OK) {
        sodium_memzero(token, strlen(token));
        free(token);
        return 1;
    }
    DiscordMessageList messages;
    DiscordStatus status;
    if (me) {
        char *self_id = NULL;
        char *self_username = NULL;
        DiscordStatus whoami_status =
            discord_get_current_user(ssl, token, &self_id, &self_username);
        if (whoami_status != DISCORD_OK || !self_id) {
            fprintf(stderr, "failed to determine your own user id for --me\n");
            scope_free(&scope);
            sodium_memzero(token, strlen(token));
            free(token);
            return 1;
        }
        status = run_search(ssl, token, &scope, content_arg, self_id, has_arg,
                            older_than_arg, newer_than_arg, &messages);
        free(self_id);
        free(self_username);
    } else {
        status = run_search(ssl, token, &scope, content_arg, author_arg,
                            has_arg, older_than_arg, newer_than_arg, &messages);
    }
    if (status != DISCORD_OK) {
        const char *msg = discord_last_error_message();
        fprintf(stderr, "search failed: http %d%s%s\n",
                discord_last_http_status(), msg ? ": " : "", msg ? msg : "");
        scope_free(&scope);
        sodium_memzero(token, strlen(token));
        free(token);
        return 1;
    }
    size_t count = messages.count;
    if (limit > 0 && (size_t)limit < count) {
        count = (size_t)limit;
    }
    const char *target_name =
        scope.channel_name ? scope.channel_name : scope.guild_name;
    printf("%zu message(s) match in %s (discord reports %d total)\n", count,
           target_name ? target_name : "(unknown)", messages.total_results);
    size_t preview_count = count < 5 ? count : 5;
    for (size_t i = 0; i < preview_count; i++) {
        const char *content_preview =
            messages.items[i].content ? messages.items[i].content : "";
        printf("  [%s] %s: %.60s\n",
               messages.items[i].timestamp ? messages.items[i].timestamp : "?",
               messages.items[i].author_id ? messages.items[i].author_id : "?",
               content_preview);
    }
    if (count > preview_count) {
        printf("  ... and %zu more\n", count - preview_count);
    }
    if (messages.total_results > 0 &&
        (size_t)messages.total_results > messages.count) {
        printf("note: discord reports more results than this single page "
               "returned; re-run purge after this batch to continue\n");
    }
    if (!confirm) {
        printf(
            "dry run: re-run with --confirm to delete these %zu message(s)\n",
            count);
        discord_message_list_free(&messages);
        scope_free(&scope);
        sodium_memzero(token, strlen(token));
        free(token);
        return 0;
    }
    size_t deleted = 0;
    size_t failed = 0;
    for (size_t i = 0; i < count; i++) {
        if (delete_with_retry(ssl, token, messages.items[i].channel_id,
                              messages.items[i].id)) {
            deleted++;
        } else {
            failed++;
        }
        printf("\rdeleted %zu/%zu (%zu failed)", deleted, count, failed);
        fflush(stdout);
    }
    printf("\n");
    discord_message_list_free(&messages);
    scope_free(&scope);
    sodium_memzero(token, strlen(token));
    free(token);
    return failed > 0 ? 1 : 0;
}
