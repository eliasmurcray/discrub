#include "discord.h"
#include "common/base64.h"
#include "common/strbuf.h"
#include "common/strutil.h"
#include "net/http.h"
#include "net/ratelimit.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static const char *host = "discord.com";

static const char *super_properties_json =
    "{\"os\":\"Windows\",\"browser\":\"Chrome\",\"device\":\"\",\"system_"
    "locale\":\"en-US\",\"browser_user_agent\":\"Mozilla/5.0 (Windows NT 10.0; "
    "Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 "
    "Safari/"
    "537.36\",\"browser_version\":\"124.0.0.0\",\"os_version\":\"10\","
    "\"referrer\":\"\",\"referring_domain\":\"\",\"referrer_current\":\"\","
    "\"referring_domain_current\":\"\",\"release_channel\":\"stable\",\"client_"
    "build_number\":324000,\"client_event_source\":null}";

typedef struct {
    http_response_t http;
    yyjson_doc *doc;
    yyjson_val *root;
} discord_resp_t;

static _Thread_local int g_last_status = 0;
static _Thread_local char *g_last_message = NULL;
static char *json_dup_str(yyjson_val *obj, const char *key);

int discord_last_http_status(void) { return g_last_status; }

const char *discord_last_error_message(void) { return g_last_message; }

static void resp_free(discord_resp_t *resp) {
    if (resp->doc) {
        yyjson_doc_free(resp->doc);
        resp->doc = NULL;
    }
    free(resp->http.body);
    resp->http.body = NULL;
    free(resp->http.bucket);
    resp->http.bucket = NULL;
}

static DiscordStatus discord_request(SSL *ssl, const char *token,
                                     const char *method, const char *path,
                                     const char *body, discord_resp_t *resp) {
    memset(resp, 0, sizeof(*resp));
    char *super_properties_b64 =
        base64_encode((const unsigned char *)super_properties_json,
                      strlen(super_properties_json));
    if (!super_properties_b64) {
        return DISCORD_ERR_JSON;
    }
    size_t sp_header_len =
        strlen("X-Super-Properties: ") + strlen(super_properties_b64) + 1;
    char *super_properties_header = malloc(sp_header_len);
    if (!super_properties_header) {
        free(super_properties_b64);
        return DISCORD_ERR_JSON;
    }
    snprintf(super_properties_header, sp_header_len, "X-Super-Properties: %s",
             super_properties_b64);
    free(super_properties_b64);
    char *auth_header = NULL;
    if (token) {
        size_t auth_header_len = strlen("Authorization: ") + strlen(token) + 1;
        auth_header = malloc(auth_header_len);
        if (!auth_header) {
            free(super_properties_header);
            return DISCORD_ERR_JSON;
        }
        snprintf(auth_header, auth_header_len, "Authorization: %s", token);
    }
    const char *headers[7];
    int header_count = 0;
    headers[header_count++] = "Accept: application/json";
    headers[header_count++] = "Accept-Language: en-US,en;q=0.9";
    headers[header_count++] =
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
    headers[header_count++] = "X-Discord-Locale: en-US";
    headers[header_count++] = super_properties_header;
    if (auth_header) {
        headers[header_count++] = auth_header;
    }
    headers[header_count] = NULL;
    http_request_t req;
    req.hostname = host;
    req.method = method;
    req.path = path;
    req.body = body;
    req.content_type = body ? "application/json" : NULL;
    req.headers = headers;
    ratelimit_wait(method, path);
    int rc = http_request(ssl, &req, &resp->http);
    free(super_properties_header);
    free(auth_header);
    if (rc != 0) {
        return DISCORD_ERR_TRANSPORT;
    }
    ratelimit_update(method, path, &resp->http);
    if (resp->http.body && resp->http.body_len > 0) {
        resp->doc = yyjson_read(resp->http.body, resp->http.body_len, 0);
        if (resp->doc) {
            resp->root = yyjson_doc_get_root(resp->doc);
        }
    }
    g_last_status = resp->http.status;
    free(g_last_message);
    g_last_message = resp->root ? json_dup_str(resp->root, "message") : NULL;
    if (resp->http.status == 401 && resp->root) {
        yyjson_val *code_val = yyjson_obj_get(resp->root, "code");
        if (yyjson_get_sint(code_val) == 60003) {
            return DISCORD_ERR_MFA_REQUIRED;
        }
    }
    if (resp->http.status == 429) {
        return DISCORD_ERR_RATE_LIMITED;
    }
    if (resp->http.status == 202) {
        return DISCORD_ERR_NOT_INDEXED;
    }
    if (resp->http.status < 200 || resp->http.status >= 300) {
        return DISCORD_ERR_HTTP;
    }
    return DISCORD_OK;
}

static char *json_dup_str(yyjson_val *obj, const char *key) {
    if (!obj) {
        return NULL;
    }
    const char *str = yyjson_get_str(yyjson_obj_get(obj, key));
    if (!str) {
        return NULL;
    }
    return dup_str(str);
}

static bool json_get_bool(yyjson_val *obj, const char *key) {
    if (!obj) {
        return false;
    }
    return yyjson_get_bool(yyjson_obj_get(obj, key));
}

static char *url_encode(const char *value) {
    if (!value) {
        return NULL;
    }
    size_t len = strlen(value);
    char *out = malloc(len * 3 + 1);
    if (!out) {
        return NULL;
    }
    size_t out_idx = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                          c == '.' || c == '~';
        if (unreserved) {
            out[out_idx++] = (char)c;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            out[out_idx++] = '%';
            out[out_idx++] = hex[c >> 4];
            out[out_idx++] = hex[c & 0x0f];
        }
    }
    out[out_idx] = '\0';
    return out;
}

static int append_query_param(strbuf_t *buf, bool *first, const char *key,
                              const char *value) {
    if (!value) {
        return 0;
    }
    char *encoded = url_encode(value);
    if (!encoded) {
        return -1;
    }
    int written =
        strbuf_appendf(buf, "%s%s=%s", *first ? "?" : "&", key, encoded);
    free(encoded);
    if (written < 0) {
        return -1;
    }
    *first = false;
    return 0;
}

static char *build_path(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        return NULL;
    }
    char *out = malloc((size_t)needed + 1);
    if (!out) {
        return NULL;
    }
    va_start(args, fmt);
    vsnprintf(out, (size_t)needed + 1, fmt, args);
    va_end(args);
    return out;
}

static int build_search_path(strbuf_t *buf, const char *base_path,
                             const DiscordSearchParams *params,
                             bool include_channel_filter) {
    if (strbuf_appendf(buf, "%s", base_path) < 0) {
        return -1;
    }
    bool first = true;
    if (append_query_param(buf, &first, "content",
                           params ? params->content : NULL) < 0) {
        return -1;
    }
    if (params) {
        for (size_t i = 0; i < params->author_id_count; i++) {
            if (append_query_param(buf, &first, "author_id",
                                   params->author_ids[i]) < 0) {
                return -1;
            }
        }
    }
    if (include_channel_filter &&
        append_query_param(buf, &first, "channel_id",
                           params ? params->channel_id : NULL) < 0) {
        return -1;
    }
    if (append_query_param(buf, &first, "has", params ? params->has : NULL) <
        0) {
        return -1;
    }
    if (append_query_param(buf, &first, "max_id",
                           params ? params->max_id : NULL) < 0) {
        return -1;
    }
    if (append_query_param(buf, &first, "min_id",
                           params ? params->min_id : NULL) < 0) {
        return -1;
    }
    if (params && params->offset > 0) {
        char offset_str[16];
        snprintf(offset_str, sizeof(offset_str), "%d", params->offset);
        if (append_query_param(buf, &first, "offset", offset_str) < 0) {
            return -1;
        }
    }
    if (include_channel_filter && params && params->include_nsfw) {
        if (append_query_param(buf, &first, "include_nsfw", "true") < 0) {
            return -1;
        }
    }
    return 0;
}

static DiscordStatus parse_message_list(yyjson_val *root,
                                        DiscordMessageList *out_list) {
    yyjson_val *messages = yyjson_obj_get(root, "messages");
    if (!messages || !yyjson_is_arr(messages)) {
        return DISCORD_ERR_JSON;
    }
    size_t total = 0;
    size_t group_idx, group_max;
    yyjson_val *group;
    yyjson_arr_foreach(messages, group_idx, group_max, group) {
        if (yyjson_is_arr(group)) {
            total += yyjson_arr_size(group);
        }
    }
    if (total == 0) {
        out_list->items = NULL;
        out_list->count = 0;
    } else {
        out_list->items = calloc(total, sizeof(DiscordMessage));
        if (!out_list->items) {
            return DISCORD_ERR_JSON;
        }
        size_t write_idx = 0;
        yyjson_arr_foreach(messages, group_idx, group_max, group) {
            if (!yyjson_is_arr(group)) {
                continue;
            }
            size_t msg_idx, msg_max;
            yyjson_val *msg;
            yyjson_arr_foreach(group, msg_idx, msg_max, msg) {
                out_list->items[write_idx].id = json_dup_str(msg, "id");
                out_list->items[write_idx].channel_id =
                    json_dup_str(msg, "channel_id");
                out_list->items[write_idx].content =
                    json_dup_str(msg, "content");
                out_list->items[write_idx].timestamp =
                    json_dup_str(msg, "timestamp");
                out_list->items[write_idx].author_id =
                    json_dup_str(yyjson_obj_get(msg, "author"), "id");
                write_idx++;
            }
        }
        out_list->count = write_idx;
    }
    out_list->total_results =
        (int)yyjson_get_int(yyjson_obj_get(root, "total_results"));
    return DISCORD_OK;
}

DiscordStatus discord_get_current_version(SSL *ssl, const char *branch,
                                          char **out_version) {
    if (!ssl || !branch || !out_version) {
        return DISCORD_ERR_HTTP;
    }
    *out_version = NULL;
    if (strcmp(branch, "canary") != 0 && strcmp(branch, "ptb") != 0 &&
        strcmp(branch, "stable") != 0) {
        return DISCORD_ERR_HTTP;
    }
    char *path = build_path("/api/updates/%s?platform=linux", branch);
    if (!path) {
        return DISCORD_ERR_JSON;
    }
    discord_resp_t resp;
    DiscordStatus status = discord_request(ssl, NULL, "GET", path, NULL, &resp);
    free(path);
    if (status != DISCORD_OK) {
        resp_free(&resp);
        return status;
    }
    if (!resp.root) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    *out_version = json_dup_str(resp.root, "name");
    resp_free(&resp);
    if (!*out_version) {
        return DISCORD_ERR_JSON;
    }
    return DISCORD_OK;
}

DiscordStatus discord_login(SSL *ssl, const char *email, const char *password,
                            DiscordLoginResult *result) {
    if (!ssl || !email || !password || !result) {
        return DISCORD_ERR_HTTP;
    }
    memset(result, 0, sizeof(*result));
    strbuf_t buf = {0};
    buf.data = malloc(0);
    if (strbuf_appendf(
            &buf, "{\"login\":\"%s\",\"password\":\"%s\",\"undelete\":false}",
            email, password) < 0) {
        strbuf_free(&buf);
        return DISCORD_ERR_JSON;
    }
    discord_resp_t resp;
    DiscordStatus status = discord_request(
        ssl, NULL, "POST", "/api/v10/auth/login", buf.data, &resp);
    strbuf_free(&buf);
    if (status == DISCORD_ERR_TRANSPORT) {
        return status;
    }
    if (!resp.root) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    if (json_get_bool(resp.root, "mfa")) {
        result->user_id = json_dup_str(resp.root, "user_id");
        result->mfa_ticket = json_dup_str(resp.root, "ticket");
        result->mfa_login_instance_id =
            json_dup_str(resp.root, "login_instance_id");
        result->mfa_totp_available = json_get_bool(resp.root, "totp");
        result->mfa_sms_available = json_get_bool(resp.root, "sms");
        result->mfa_backup_available = json_get_bool(resp.root, "backup");
        resp_free(&resp);
        return DISCORD_ERR_MFA_REQUIRED;
    }
    result->token = json_dup_str(resp.root, "token");
    result->user_id = json_dup_str(resp.root, "user_id");
    resp_free(&resp);
    if (!result->token) {
        return DISCORD_ERR_HTTP;
    }
    return DISCORD_OK;
}

DiscordStatus discord_verify_mfa(SSL *ssl, const char *authenticator_type,
                                 const char *ticket,
                                 const char *login_instance_id,
                                 const char *code, DiscordLoginResult *result) {
    if (!ssl || !authenticator_type || !ticket || !code || !result) {
        return DISCORD_ERR_HTTP;
    }
    memset(result, 0, sizeof(*result));
    strbuf_t buf = {0};
    buf.data = malloc(0);
    int append_status;
    if (login_instance_id) {
        append_status = strbuf_appendf(
            &buf,
            "{\"ticket\":\"%s\",\"login_instance_id\":\"%s\",\"code\":\"%s\"}",
            ticket, login_instance_id, code);
    } else {
        append_status = strbuf_appendf(
            &buf, "{\"ticket\":\"%s\",\"code\":\"%s\"}", ticket, code);
    }
    if (append_status < 0) {
        strbuf_free(&buf);
        return DISCORD_ERR_JSON;
    }
    char *path = build_path("/api/v10/auth/mfa/%s", authenticator_type);
    if (!path) {
        strbuf_free(&buf);
        return DISCORD_ERR_JSON;
    }
    discord_resp_t resp;
    DiscordStatus status =
        discord_request(ssl, NULL, "POST", path, buf.data, &resp);
    free(path);
    strbuf_free(&buf);
    if (status == DISCORD_ERR_TRANSPORT) {
        return status;
    }
    if (!resp.root) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    result->token = json_dup_str(resp.root, "token");
    resp_free(&resp);
    if (!result->token) {
        return DISCORD_ERR_HTTP;
    }
    return DISCORD_OK;
}

DiscordStatus discord_get_current_user(SSL *ssl, const char *token,
                                       char **out_user_id,
                                       char **out_username) {
    if (!ssl || !token || !out_user_id || !out_username) {
        return DISCORD_ERR_HTTP;
    }
    *out_user_id = NULL;
    *out_username = NULL;
    discord_resp_t resp;
    DiscordStatus status =
        discord_request(ssl, token, "GET", "/api/v10/users/@me", NULL, &resp);
    if (status != DISCORD_OK) {
        resp_free(&resp);
        return status;
    }
    if (!resp.root) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    *out_user_id = json_dup_str(resp.root, "id");
    *out_username = json_dup_str(resp.root, "username");
    resp_free(&resp);
    return DISCORD_OK;
}

DiscordStatus discord_get_guilds(SSL *ssl, const char *token,
                                 DiscordGuildList *out_list) {
    if (!ssl || !token || !out_list) {
        return DISCORD_ERR_HTTP;
    }
    memset(out_list, 0, sizeof(*out_list));
    discord_resp_t resp;
    DiscordStatus status = discord_request(
        ssl, token, "GET", "/api/v10/users/@me/guilds", NULL, &resp);
    if (status != DISCORD_OK) {
        resp_free(&resp);
        return status;
    }
    if (!resp.root || !yyjson_is_arr(resp.root)) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    size_t count = yyjson_arr_size(resp.root);
    if (count == 0) {
        resp_free(&resp);
        return DISCORD_OK;
    }
    out_list->items = calloc(count, sizeof(DiscordGuild));
    if (!out_list->items) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    size_t idx, max;
    yyjson_val *item;
    yyjson_arr_foreach(resp.root, idx, max, item) {
        out_list->items[idx].id = json_dup_str(item, "id");
        out_list->items[idx].name = json_dup_str(item, "name");
    }
    out_list->count = count;
    resp_free(&resp);
    return DISCORD_OK;
}

DiscordStatus discord_get_guild_channels(SSL *ssl, const char *token,
                                         const char *guild_id,
                                         DiscordChannelList *out_list) {
    if (!ssl || !token || !guild_id || !out_list) {
        return DISCORD_ERR_HTTP;
    }
    memset(out_list, 0, sizeof(*out_list));
    char *path = build_path("/api/v10/guilds/%s/channels", guild_id);
    if (!path) {
        return DISCORD_ERR_JSON;
    }
    discord_resp_t resp;
    DiscordStatus status =
        discord_request(ssl, token, "GET", path, NULL, &resp);
    free(path);
    if (status != DISCORD_OK) {
        resp_free(&resp);
        return status;
    }
    if (!resp.root || !yyjson_is_arr(resp.root)) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    size_t count = yyjson_arr_size(resp.root);
    if (count == 0) {
        resp_free(&resp);
        return DISCORD_OK;
    }
    out_list->items = calloc(count, sizeof(DiscordChannel));
    if (!out_list->items) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    size_t idx, max;
    yyjson_val *item;
    yyjson_arr_foreach(resp.root, idx, max, item) {
        out_list->items[idx].id = json_dup_str(item, "id");
        out_list->items[idx].name = json_dup_str(item, "name");
        out_list->items[idx].type =
            (int)yyjson_get_int(yyjson_obj_get(item, "type"));
    }
    out_list->count = count;
    resp_free(&resp);
    return DISCORD_OK;
}

DiscordStatus discord_get_dm_channels(SSL *ssl, const char *token,
                                      DiscordChannelList *out_list) {
    if (!ssl || !token || !out_list) {
        return DISCORD_ERR_HTTP;
    }
    memset(out_list, 0, sizeof(*out_list));
    discord_resp_t resp;
    DiscordStatus status = discord_request(
        ssl, token, "GET", "/api/v10/users/@me/channels", NULL, &resp);
    if (status != DISCORD_OK) {
        resp_free(&resp);
        return status;
    }
    if (!resp.root || !yyjson_is_arr(resp.root)) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    size_t count = yyjson_arr_size(resp.root);
    if (count == 0) {
        resp_free(&resp);
        return DISCORD_OK;
    }
    out_list->items = calloc(count, sizeof(DiscordChannel));
    if (!out_list->items) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    size_t idx, max;
    yyjson_val *item;
    yyjson_arr_foreach(resp.root, idx, max, item) {
        out_list->items[idx].id = json_dup_str(item, "id");
        out_list->items[idx].name = json_dup_str(item, "name");
        if (!out_list->items[idx].name) {
            out_list->items[idx].name = json_dup_str(
                yyjson_arr_get_first(yyjson_obj_get(item, "recipients")),
                "username");
        }
        out_list->items[idx].type =
            (int)yyjson_get_int(yyjson_obj_get(item, "type"));
    }
    out_list->count = count;
    resp_free(&resp);
    return DISCORD_OK;
}

DiscordStatus discord_search_guild_messages(SSL *ssl, const char *token,
                                            const char *guild_id,
                                            const DiscordSearchParams *params,
                                            DiscordMessageList *out_list,
                                            int *out_retry_after_ms) {
    if (!ssl || !token || !guild_id || !out_list) {
        return DISCORD_ERR_HTTP;
    }
    memset(out_list, 0, sizeof(*out_list));
    if (out_retry_after_ms) {
        *out_retry_after_ms = 0;
    }
    strbuf_t path_buf = {0};
    path_buf.data = malloc(0);
    char *base_path =
        build_path("/api/v10/guilds/%s/messages/search", guild_id);
    if (!base_path) {
        strbuf_free(&path_buf);
        return DISCORD_ERR_JSON;
    }
    int build_status = build_search_path(&path_buf, base_path, params, true);
    free(base_path);
    if (build_status < 0) {
        strbuf_free(&path_buf);
        return DISCORD_ERR_JSON;
    }
    discord_resp_t resp;
    DiscordStatus status =
        discord_request(ssl, token, "GET", path_buf.data, NULL, &resp);
    strbuf_free(&path_buf);
    if (status == DISCORD_ERR_NOT_INDEXED) {
        if (out_retry_after_ms && resp.root) {
            *out_retry_after_ms = (int)(yyjson_get_real(yyjson_obj_get(
                                            resp.root, "retry_after")) *
                                        1000.0);
        }
        resp_free(&resp);
        return status;
    }
    if (status != DISCORD_OK) {
        resp_free(&resp);
        return status;
    }
    if (!resp.root) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    DiscordStatus parse_status = parse_message_list(resp.root, out_list);
    resp_free(&resp);
    return parse_status;
}

DiscordStatus discord_search_channel_messages(SSL *ssl, const char *token,
                                              const char *channel_id,
                                              const DiscordSearchParams *params,
                                              DiscordMessageList *out_list,
                                              int *out_retry_after_ms) {
    if (!ssl || !token || !channel_id || !out_list) {
        return DISCORD_ERR_HTTP;
    }
    memset(out_list, 0, sizeof(*out_list));
    if (out_retry_after_ms) {
        *out_retry_after_ms = 0;
    }
    strbuf_t path_buf = {0};
    path_buf.data = malloc(0);
    char *base_path =
        build_path("/api/v10/channels/%s/messages/search", channel_id);
    if (!base_path) {
        strbuf_free(&path_buf);
        return DISCORD_ERR_JSON;
    }
    int build_status = build_search_path(&path_buf, base_path, params, false);
    free(base_path);
    if (build_status < 0) {
        strbuf_free(&path_buf);
        return DISCORD_ERR_JSON;
    }
    discord_resp_t resp;
    DiscordStatus status =
        discord_request(ssl, token, "GET", path_buf.data, NULL, &resp);
    strbuf_free(&path_buf);
    if (status == DISCORD_ERR_NOT_INDEXED) {
        if (out_retry_after_ms && resp.root) {
            *out_retry_after_ms = (int)(yyjson_get_real(yyjson_obj_get(
                                            resp.root, "retry_after")) *
                                        1000.0);
        }
        resp_free(&resp);
        return status;
    }
    if (status != DISCORD_OK) {
        resp_free(&resp);
        return status;
    }
    if (!resp.root) {
        resp_free(&resp);
        return DISCORD_ERR_JSON;
    }
    DiscordStatus parse_status = parse_message_list(resp.root, out_list);
    resp_free(&resp);
    return parse_status;
}

DiscordStatus discord_delete_message(SSL *ssl, const char *token,
                                     const char *channel_id,
                                     const char *message_id) {
    if (!ssl || !token || !channel_id || !message_id) {
        return DISCORD_ERR_HTTP;
    }
    char *path =
        build_path("/api/v10/channels/%s/messages/%s", channel_id, message_id);
    if (!path) {
        return DISCORD_ERR_JSON;
    }
    discord_resp_t resp;
    DiscordStatus status =
        discord_request(ssl, token, "DELETE", path, NULL, &resp);
    free(path);
    resp_free(&resp);
    return status;
}

void discord_login_result_free(DiscordLoginResult *result) {
    if (!result) {
        return;
    }
    free(result->user_id);
    free(result->token);
    free(result->mfa_ticket);
    free(result->mfa_login_instance_id);
    memset(result, 0, sizeof(*result));
}

void discord_guild_list_free(DiscordGuildList *list) {
    if (!list) {
        return;
    }
    for (size_t idx = 0; idx < list->count; idx++) {
        free(list->items[idx].id);
        free(list->items[idx].name);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

void discord_channel_list_free(DiscordChannelList *list) {
    if (!list) {
        return;
    }
    for (size_t idx = 0; idx < list->count; idx++) {
        free(list->items[idx].id);
        free(list->items[idx].name);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

void discord_message_list_free(DiscordMessageList *list) {
    if (!list) {
        return;
    }
    for (size_t idx = 0; idx < list->count; idx++) {
        free(list->items[idx].id);
        free(list->items[idx].channel_id);
        free(list->items[idx].author_id);
        free(list->items[idx].content);
        free(list->items[idx].timestamp);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}
