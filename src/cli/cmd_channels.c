#include "cli.h"
#include "discord/discord.h"
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
     .description = "List channels in this guild (name or id)"},
    {.identifier = 'd',
     .access_letters = "d",
     .access_name = "dm",
     .description = "List DM / group DM channels instead"},
    {.identifier = 'h',
     .access_letters = "h",
     .access_name = "help",
     .description = "Show this help"},
};

static const char *channel_type_name(int type) {
    switch (type) {
    case 0:
        return "text";
    case 1:
        return "dm";
    case 2:
        return "voice";
    case 3:
        return "group-dm";
    case 4:
        return "category";
    case 5:
        return "announcement";
    case 10:
    case 11:
    case 12:
        return "thread";
    case 13:
        return "stage";
    case 15:
        return "forum";
    case 16:
        return "media";
    default:
        return "other";
    }
}

int cmd_channels(SSL *ssl, int argc, char **argv) {
    const char *guild_arg = NULL;
    bool dm = false;
    cag_option_context context;
    cag_option_init(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
    while (cag_option_fetch(&context)) {
        switch (cag_option_get_identifier(&context)) {
        case 'g':
            guild_arg = cag_option_get_value(&context);
            break;
        case 'd':
            dm = true;
            break;
        case 'h':
            printf("Usage: xcord channels [OPTION]...\n");
            printf(
                "List channels in a guild, or your DM/group DM channels.\n\n");
            cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
            return 0;
        case '?':
            cag_option_print_error(&context, stdout);
            return 1;
        }
    }
    if (!dm && !guild_arg) {
        fprintf(stderr, "specify --guild or --dm\n");
        return 1;
    }
    char *token = cli_load_token();
    if (!token) {
        return 1;
    }
    DiscordChannelList channels;
    DiscordStatus status;
    char *guild_id = NULL;
    char *guild_name = NULL;
    if (dm) {
        status = discord_get_dm_channels(ssl, token, &channels);
    } else {
        ScopeStatus scope_status =
            scope_resolve_guild(ssl, token, guild_arg, &guild_id, &guild_name);
        if (scope_status != SCOPE_OK) {
            sodium_memzero(token, strlen(token));
            free(token);
            return 1;
        }
        status = discord_get_guild_channels(ssl, token, guild_id, &channels);
    }
    if (status != DISCORD_OK) {
        const char *msg = discord_last_error_message();
        fprintf(stderr, "failed to list channels: http %d%s%s\n",
                discord_last_http_status(), msg ? ": " : "", msg ? msg : "");
        free(guild_id);
        free(guild_name);
        sodium_memzero(token, strlen(token));
        free(token);
        return 1;
    }
    printf("channels in %s:\n",
           dm ? "your DMs" : (guild_name ? guild_name : "(unknown)"));
    for (size_t i = 0; i < channels.count; i++) {
        printf("  %-20s  %-12s  %s\n", channels.items[i].id,
               channel_type_name(channels.items[i].type),
               channels.items[i].name ? channels.items[i].name : "(unnamed)");
    }
    discord_channel_list_free(&channels);
    free(guild_id);
    free(guild_name);
    sodium_memzero(token, strlen(token));
    free(token);
    return 0;
}
