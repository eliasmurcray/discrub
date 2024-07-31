#include "discrub_interface.h"
#include "openssl_helpers.h"

static int assert_snowflake(const char* snowflake_id) {
  if (!snowflake_id) return 1;
  errno = 0;
  char *endptr = NULL;
  strtoull(snowflake_id, &endptr, 10);
  return !errno && *endptr != '\0';
}

static char* get_params(struct SearchOpts *opts) {
  size_t params_size = 1;
  if (opts->author_id) {
    params_size += strlen("author_id=") + strlen(opts->author_id) + 1;
  }
  if (opts->channel_id) {
    params_size += strlen("channel_id=") + strlen(opts->channel_id) + 1;
  }
  if (opts->include_nsfw) {
    params_size += strlen("include_nsfw=") + strlen(opts->include_nsfw ? "true" : "false") + 1;
  }
  if (opts->offset) {
    char *uint_str = malloc(12);
    if (!uint_str) return NULL;
    snprintf(uint_str, 12, "%u", opts->offset);
    params_size += strlen("offset=") + strlen(uint_str) + 1;
    free(uint_str);
  }
  if (params_size == 1) return NULL;
  char *params = malloc(params_size);
  if (!params) return NULL;
  params[0] = '\0';
  if (opts->author_id) {
    strcat(params, "author_id=");
    strcat(params, opts->author_id);
    strcat(params, "&");
  }
  if (opts->channel_id) {
    strcat(params, "channel_id=");
    strcat(params, opts->channel_id);
    strcat(params, "&");
  }
  if (opts->include_nsfw) {
    strcat(params, "include_nsfw=");
    strcat(params, opts->include_nsfw ? "true" : "false");
    strcat(params, "&");
  }
  if (opts->offset) {
    char *uint_str = malloc(12);
    if (!uint_str) return NULL;
    snprintf(uint_str, 12, "%u", opts->offset);
    strcat(params, "offset=");
    strcat(params, uint_str);
    free(uint_str);
    strcat(params, "&");
  }
  params[params_size - 2] = '\0';
  return params;
}

unsigned char discrub_delete_message(BIO *bio, const char *token, const char *channel_id, const char *message_id, char **error) {
  if (!bio) {
    *error = "Invalid BIO object";
    return 1;
  }
  if (assert_snowflake(channel_id)) {
    *error = "Invalid guild_id";
    return 1;
  }
  if (assert_snowflake(message_id)) {
    *error = "Invalid message_id";
    return 1;
  }
  if (!token) {
    *error = "Invalid auth token";
    return 1;
  }
  const char* request_fmt = "DELETE /api/v9/channels/%s/messages/%s HTTP/1.1\r\n"
                            "Host: discord.com\r\n"
                            "Authorization: %s\r\n"
                            "Connection: close\r\n"
                            "\r\n";
  size_t request_size = snprintf(NULL, 0, request_fmt, channel_id, message_id, token) + 1;
  char *request_string = malloc(request_size);
  if (0 > snprintf(request_string, request_size, request_fmt, channel_id, message_id, token)) {
    *error = "Failed to format request string";
    return 1;
  }
  char *response_string = send_request(bio, request_string, error);
  free(request_string);
  if (!response_string) return 1;
  struct HTTPResponse *response = parse_response(response_string, error);
  free(response_string);
  if (!response) return 1;
  if (response->code != 204) {
    size_t error_size = snprintf(NULL, 0, "Status code is %hu", response->code) + 1;
    char* buffer = malloc(error_size);
    snprintf(buffer, error_size, "Status code is %hu", response->code);
    *error = buffer;
    return 1;
  }
  return 0;
}

struct SearchResponse* discrub_search_messages(BIO *bio, const char* token, const char *server_id, struct SearchOpts *opts, char **error) {
  if (!bio) {
    *error = "Invalid BIO object";
    return NULL;
  }
  if (!token) {
    *error = "Invalid auth token";
    return NULL;
  }
  if (assert_snowflake(server_id)) {
    *error = "Invalid server_id";
    return NULL;
  }
  char *params = get_params(opts);
  if (!params) {
    *error = "Failed to generate params from SearchOpts";
    return NULL;
  }
  const char* request_fmt = "GET /api/v9/guilds/%s/messages/search?%s HTTP/1.1\r\n"
                            "Host: discord.com\r\n"
                            "Authorization: %s\r\n"
                            "Connection: close\r\n"
                            "\r\n";
  size_t request_size = snprintf(NULL, 0, request_fmt, server_id, params, token) + 1;
  char *request_string = malloc(request_size);
  if (0 > snprintf(request_string, request_size, request_fmt, server_id, params, token)) {
    *error = "Failed to format request string";
    return NULL;
  }
  char *response_string = send_request(bio, request_string, error);
  free(request_string);
  if (!response_string) return NULL;
  struct HTTPResponse *response = parse_response(response_string, error);
  free(response_string);
  if (!response) return NULL;
  if (response->code != 200) {
    size_t error_size = snprintf(NULL, 0, "Status code is %hu", response->code) + 1;
    char *buffer = malloc(error_size);
    snprintf(buffer, error_size, "Status code is %hu", response->code);
    *error = buffer;
    return NULL;
  }
  if (!strtok(response->data, "\n")) {
    *error = "Failed to extract json from response";
    return NULL;
  }
  const char *json_start = strtok(NULL, "\n");
  const char* json_end = strtok(NULL, "\n");
  if (!json_start || !json_end) {
    *error = "Failed to extract json from response";
    return NULL;
  }
  size_t json_length = json_end - json_start;
  char *json = malloc(json_length + 1);
  if (!json) {
    *error = "Failed to malloc json string";
    return NULL;
  }
  strncpy(json, json_start, json_length);
  json[json_length] = '\0';
  return NULL;
}
