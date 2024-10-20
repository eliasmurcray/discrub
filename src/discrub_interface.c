#include "discrub_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_param(char **params, size_t *size, const char *key,
                      const char *value) {
  if (value) {
    size_t len = strlen(key) + strlen(value) + 2;
    *params = realloc(*params, *size + len);
    if (*params) {
      strcat(*params, key);
      strcat(*params, "=");
      strcat(*params, value);
      strcat(*params, "&");
      *size += len;
    }
  }
}

static char *get_params(struct SearchOptions *options) {
  size_t params_size = 1;
  char *params = malloc(params_size);
  if (!params) return NULL;
  params[0] = '\0';

  add_param(&params, &params_size, "author_id", options->author_id);
  add_param(&params, &params_size, "channel_id", options->channel_id);
  add_param(&params, &params_size, "content", options->content);
  add_param(&params, &params_size, "mentions", options->mentions);
  add_param(&params, &params_size, "include_nsfw",
            options->include_nsfw ? "true" : "false");
  add_param(&params, &params_size, "pinned",
            options->pinned ? "true" : "false");

  if (options->offset) {
    char uint_str[12];
    snprintf(uint_str, sizeof(uint_str), "%lu", options->offset);
    add_param(&params, &params_size, "offset", uint_str);
  }

  if (params_size == 1) {
    free(params);
    return NULL;
  }

  params[params_size - 2] = '\0';
  return params;
}

static char *fmt_timestamp(const char* timestamp) {
  struct tm tm;
  memset(&tm, 0, sizeof(struct tm));
  if (strptime(timestamp, "%Y-%m-%dT%H:%M:%S", &tm) == NULL) return NULL;
  time_t time_utc = mktime(&tm);
  if (time_utc == -1) return NULL;
  struct tm *local_tm = localtime(&time_utc);
  char buffer[64];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_tm);
  char *new_timestamp = malloc(strlen(buffer) + 1);
  if (!new_timestamp) return NULL;
  strncpy(new_timestamp, buffer, strlen(buffer));
  return new_timestamp;
}

bool discrub_delete_message(BIO *connection, const char *token,
                            const char *channel_id, const char *message_id,
                            enum DiscrubError *error) {
  if (!connection || !token || !channel_id || !message_id) {
    *error = DISCRUB_EARGS;
    return 1;
  }
  const char *request_fmt =
      "DELETE /api/v9/channels/%s/messages/%s HTTP/1.1\r\n"
      "Host: discord.com\r\n"
      "Authorization: %s\r\n"
      "Connection: close\r\n"
      "\r\n";
  size_t request_size =
      snprintf(NULL, 0, request_fmt, channel_id, message_id, token) + 1;
  char *request_string = malloc(request_size);
  if (!request_string) {
    *error = DISCRUB_ENOMEM;
    return false;
  }
  snprintf(request_string, request_size, request_fmt, channel_id, message_id,
           token);
  enum HTTPError *http_error = NULL;
  struct HTTPResponse *response =
      http_request(connection, request_string, http_error);
  if (!response) {
    printf("Error while fetching: %s\n\nWith request: %s\n",
           http_strerror(http_error), request_string);
    free(request_string);
    *error = DISCRUB_EHTTP;
    return false;
  }
  free(request_string);
  if (!response) {
    printf("Failed to delete message: No response\n");
    return 1;
  }
  if (response->code != 204) {
    printf("Failed to delete message: Status code is %hu\n", response->code);
    return 1;
  }
  return 0;
}

struct SearchResponse *discrub_search(BIO *connection, const char *token,
                                      const char *server_id,
                                      struct SearchOptions *options,
                                      enum DiscrubError *error) {
  if (!connection || !token || !server_id || !options) {
    *error = DISCRUB_EARGS;
    return NULL;
  }

  char *params = get_params(options);
  if (!params) {
    *error = DISCRUB_EARGS;
    return NULL;
  }
  const char *request_fmt =
      "GET /api/v9/guilds/%s/messages/search?%s HTTP/1.1\r\n"
      "Host: discord.com\r\n"
      "Authorization: %s\r\n"
      "Connection: close\r\n"
      "\r\n";
  size_t request_size =
      snprintf(NULL, 0, request_fmt, server_id, params, token) + 1;
  char *request_string = malloc(request_size);
  snprintf(request_string, request_size, request_fmt, server_id, params, token);
  free(params);
  enum HTTPError *http_error = NULL;
  struct HTTPResponse *response =
      http_request(connection, request_string, http_error);
  if (http_error) {
    printf("Error while fetching: %s\n\nWith request: %s\n",
           http_strerror(http_error), request_string);
    free(request_string);
    *error = DISCRUB_EHTTP;
    return NULL;
  }
  free(request_string);
  if (!response) {
    printf("Failed to search: No response\n");
    return NULL;
  }
  if (response->code != 200) {
    printf("Failed to search: Status code is %hu\n", response->code);
    free(response);
    return NULL;
  }

  if (!strtok(response->data, "\n")) {
    free(response);
    *error = DISCRUB_EHTTP;
    return NULL;
  }

  const char *json_start = strtok(NULL, "\n");
  const char *json_end = strtok(NULL, "\n");

  if (!json_start || !json_end) {
    free(response);
    *error = DISCRUB_EHTTP;
    return NULL;
  }

  size_t json_length = json_end - json_start;
  char *json_string = malloc(json_length + 1);
  if (!json_string) {
    free(response);
    *error = DISCRUB_ENOMEM;
    return NULL;
  }
  strncpy(json_string, json_start, json_length);
  json_string[json_length] = '\0';
  free(response);

  enum JsonError json_error;
  struct JsonToken *response_object = jsontok_parse(json_string, &json_error);
  if (!response_object) {
    printf("Error parsing response JSON: %s\n", jsontok_strerror(json_error));
    *error = DISCRUB_EPARSE;
    return NULL;
  }

  if (response_object->type != JSON_OBJECT) {
    *error = DISCRUB_EPARSE;
    return NULL;
  }

  struct JsonToken *messages_subarray = jsontok_get(response_object->as_object, "messages");
  if (!messages_subarray) {
    *error = DISCRUB_EPARSE;
    return NULL;
  }
  if (messages_subarray->type != JSON_WRAPPED_ARRAY) {
    jsontok_free(response_object);
    *error = DISCRUB_EPARSE;
    return NULL;
  }

  struct JsonToken *messages_array = jsontok_parse(messages_subarray->as_string, &json_error);
  if (!messages_array) {
    jsontok_free(messages_subarray);
    jsontok_free(response_object);
    *error = DISCRUB_EPARSE;
    return NULL;
  }

  if (messages_array->type != JSON_ARRAY) {
    jsontok_free(messages_subarray);
    jsontok_free(response_object);
    *error = DISCRUB_EPARSE;
    return NULL;
  }

  struct JsonArray *message_containers = messages_array->as_array;
  printf("Length of message_containers array: %zu\n", message_containers->length);
  size_t i = 0;
  for (; i < message_containers->length; i++) {
    struct JsonToken *message_container_token = message_containers->elements[i];
    if (!message_container_token->as_string) {
      printf("Message container token at %zu is NULL\n", i);
      break;
    }
    struct JsonToken *message_container_array = jsontok_parse(message_container_token->as_string, &json_error);
    if (!message_container_array) {
      printf("Error in parsing message container subarray at index %zu: %s\n\n%s\n", i, jsontok_strerror(json_error), message_container_token->as_string);
      break;
    }
    if (message_container_array->type != JSON_ARRAY || message_container_array->as_array->length != 1) {
      jsontok_free(message_container_array);
      break;
    }
    struct JsonToken *message_token = message_container_array->as_array->elements[0];
    if (message_token->type != JSON_WRAPPED_OBJECT) {
      jsontok_free(message_container_array);
      break;
    }
    struct JsonToken *message_object = jsontok_parse(message_token->as_string, &json_error);
    if (!message_object) {
      printf("Error in parsing message token at index %zu: %s\n\n%s\n", i, jsontok_strerror(json_error), message_token->as_string);
      jsontok_free(message_container_array);
      break;
    }
    struct JsonToken *author_token = jsontok_get(message_object->as_object, "author");
    if (!author_token || author_token->type != JSON_WRAPPED_OBJECT) {
      jsontok_free(message_container_array);
      break;
    }
    struct JsonToken *author_object = jsontok_parse(author_token->as_string, &json_error);
    if (!author_object) {
      jsontok_free(message_container_array);
      break;
    }
    char *author_id = jsontok_get(author_object->as_object, "id")->as_string;
    char *author_username = jsontok_get(author_object->as_object, "username")->as_string;
    char *content = jsontok_get(message_object->as_object, "content")->as_string;
    char *id = jsontok_get(message_object->as_object, "id")->as_string;
    char *timestamp = jsontok_get(message_object->as_object, "timestamp")->as_string;
    char *formatted = fmt_timestamp(timestamp);
    printf("[%s] %s (%s) %s {%s}\n", formatted, author_id, author_username, content, id);
    jsontok_free(message_container_array);
  }
  jsontok_free(messages_array);
  jsontok_free(response_object);
  free(json_string);
  return NULL;
}

const char *discrub_strerror(enum DiscrubError *error) {
  switch (*error) {
    case DISCRUB_ENOMEM: return "Memory allocation failed";
    case DISCRUB_EARGS: return "Invalid arguments provided";
    case DISCRUB_EHTTP: return "HTTP request failed";
    default: return "Unknown Discrub error";
  }
}
