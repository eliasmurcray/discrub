#pragma once
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DISCORD_OK = 0,
    DISCORD_ERR_TRANSPORT,
    DISCORD_ERR_HTTP,
    DISCORD_ERR_JSON,
    DISCORD_ERR_MFA_REQUIRED,
    DISCORD_ERR_RATE_LIMITED,
    DISCORD_ERR_NOT_INDEXED,
} DiscordStatus;

typedef struct {
    char *user_id;
    char *token;
    char *mfa_ticket;
    char *mfa_login_instance_id;
    bool mfa_totp_available;
    bool mfa_sms_available;
    bool mfa_backup_available;
} DiscordLoginResult;

typedef struct {
    char *id;
    char *name;
} DiscordGuild;

typedef struct {
    DiscordGuild *items;
    size_t count;
} DiscordGuildList;

typedef struct {
    char *id;
    char *name;
    int type;
} DiscordChannel;

typedef struct {
    DiscordChannel *items;
    size_t count;
} DiscordChannelList;

typedef struct {
    char *id;
    char *channel_id;
    char *author_id;
    char *content;
    char *timestamp;
} DiscordMessage;

typedef struct {
    DiscordMessage *items;
    size_t count;
    int total_results;
} DiscordMessageList;

typedef struct {
    const char *content;
    const char *const *author_ids;
    size_t author_id_count;
    const char *channel_id;
    const char *has;
    const char *max_id;
    const char *min_id;
    int offset;
    bool include_nsfw;
} DiscordSearchParams;

DiscordStatus discord_get_current_version(SSL *ssl, const char *branch, char **out_version);

int discord_last_http_status(void);
const char *discord_last_error_message(void);

DiscordStatus discord_login(SSL *ssl, const char *email, const char *password, DiscordLoginResult *result);
DiscordStatus discord_verify_mfa(SSL *ssl, const char *authenticator_type, const char *ticket, const char *login_instance_id, const char *code, DiscordLoginResult *result);

DiscordStatus discord_get_current_user(SSL *ssl, const char *token, char **out_user_id, char **out_username);

DiscordStatus discord_get_guilds(SSL *ssl, const char *token, DiscordGuildList *out_list);
DiscordStatus discord_get_guild_channels(SSL *ssl, const char *token, const char *guild_id, DiscordChannelList *out_list);
DiscordStatus discord_get_dm_channels(SSL *ssl, const char *token, DiscordChannelList *out_list);

DiscordStatus discord_search_guild_messages(SSL *ssl, const char *token, const char *guild_id, const DiscordSearchParams *params, DiscordMessageList *out_list, int *out_retry_after_ms);
DiscordStatus discord_search_channel_messages(SSL *ssl, const char *token, const char *channel_id, const DiscordSearchParams *params, DiscordMessageList *out_list, int *out_retry_after_ms);

DiscordStatus discord_delete_message(SSL *ssl, const char *token, const char *channel_id, const char *message_id);

void discord_login_result_free(DiscordLoginResult *result);
void discord_guild_list_free(DiscordGuildList *list);
void discord_channel_list_free(DiscordChannelList *list);
void discord_message_list_free(DiscordMessageList *list);
