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

static char *fmt_timestamp(const char *timestamp) {
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
  new_timestamp[strlen(buffer)] = '\0';
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
  enum HTTPError http_error = HTTP_ENOERR;
  struct HTTPResponse *response =
      http_request(connection, request_string, &http_error);
  if (http_error) {
    printf("Error while fetching: %s\n\nWith request: %s\n",
           http_strerror(&http_error), request_string);
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
  enum HTTPError http_error = HTTP_ENOERR;
  struct HTTPResponse *response =
      http_request(connection, request_string, &http_error);
  if (http_error) {
    printf("Error while fetching: %s\n\nWith request: %s\n",
           http_strerror(&http_error), request_string);
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
  struct SearchResponse *search_response = malloc(sizeof(struct SearchResponse));
  if (!search_response) {
    jsontok_free(messages_subarray);
    jsontok_free(response_object);
    *error = DISCRUB_ENOMEM;
    return NULL;
  }

  search_response->messages = malloc(message_containers->length * sizeof(struct DiscordMessage));
  if (!search_response->messages) {
    free(search_response);
    jsontok_free(messages_subarray);
    jsontok_free(response_object);
    *error = DISCRUB_ENOMEM;
    return NULL;
  }

  search_response->length = message_containers->length;
  size_t i = 0;
  for (; i < message_containers->length; i++) {
    struct JsonToken *message_container_token = message_containers->elements[i];
    if (!message_container_token->as_string) {
      printf("Message container token at %zu is NULL\n", i);
      break;
    }

    struct JsonToken *message_container_array = jsontok_parse(message_container_token->as_string, &json_error);
    if (!message_container_array ||
        message_container_array->type != JSON_ARRAY ||
        message_container_array->as_array->length != 1) {
      printf("Invalid message container array at index %zu\n", i);
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
      printf("Error in parsing message token at index %zu: %s\n\n%s\n",
             i, jsontok_strerror(json_error), message_token->as_string);
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

    struct JsonToken *id_token = jsontok_get(message_object->as_object, "id");
    struct JsonToken *content_token = jsontok_get(message_object->as_object, "content");
    struct JsonToken *timestamp_token = jsontok_get(message_object->as_object, "timestamp");
    struct JsonToken *author_id_token = jsontok_get(author_object->as_object, "id");
    struct JsonToken *author_username_token = jsontok_get(author_object->as_object, "username");

    if (!id_token || id_token->type != JSON_STRING ||
        !content_token || content_token->type != JSON_STRING ||
        !timestamp_token || timestamp_token->type != JSON_STRING ||
        !author_id_token || author_id_token->type != JSON_STRING ||
        !author_username_token || author_username_token->type != JSON_STRING) {
      printf("Missing or invalid fields in message object at index %zu\n", i);
      jsontok_free(message_container_array);
      break;
    }

    struct DiscordMessage *message = &search_response->messages[i];
    message->id = malloc(strlen(id_token->as_string) + 1);
    if (!message->id) {
      free(message);
      *error = DISCRUB_ENOMEM;
      return NULL;
    }
    strcpy(message->id, id_token->as_string);

    message->content = malloc(strlen(content_token->as_string) + 1);
    if (!message->content) {
      free(message->id);
      free(message);
      *error = DISCRUB_ENOMEM;
      return NULL;
    }
    strcpy(message->content, content_token->as_string);

    message->timestamp = malloc(strlen(fmt_timestamp(timestamp_token->as_string)) + 1);
    if (!message->timestamp) {
      free(message->content);
      free(message->id);
      free(message);
      *error = DISCRUB_ENOMEM;
      return NULL;
    }
    strcpy(message->timestamp, fmt_timestamp(timestamp_token->as_string));

    message->author_id = malloc(strlen(author_id_token->as_string) + 1);
    if (!message->author_id) {
      free(message->timestamp);
      free(message->content);
      free(message->id);
      free(message);
      *error = DISCRUB_ENOMEM;
      return NULL;
    }
    strcpy(message->author_id, author_id_token->as_string);

    message->author_username = malloc(strlen(author_username_token->as_string) + 1);
    if (!message->author_username) {
      free(message->author_id);
      free(message->timestamp);
      free(message->content);
      free(message->id);
      free(message);
      *error = DISCRUB_ENOMEM;
      return NULL;
    }
    strcpy(message->author_username, author_username_token->as_string);

    jsontok_free(message_container_array);
  }

  jsontok_free(messages_array);
  jsontok_free(response_object);
  free(json_string);
  return search_response;
}

void discrub_free_search_response(struct SearchResponse *response) {
  if (!response) return;

  size_t i = 0;
  for (; i < response->length; i++) {
    struct DiscordMessage *message = &response->messages[i];
    free(message->id);
    free(message->content);
    free(message->timestamp);
    free(message->author_id);
    free(message->author_username);
  }

  free(response->messages);
  free(response);
}

struct LoginResponse *discrub_login(BIO *connection, const char *username, const char *password, enum DiscrubError *error) {
  if (!connection || !username || !password) {
    *error = DISCRUB_EARGS;
    return NULL;
  }
  const char *request_fmt =
      "POST /api/v9/auth/login HTTP/1.1\r\n"
      "Host: discord.com\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %d\r\n"
      "Connection: close\r\n"
      "\r\n"
      "{"
      "\"gift_code_sku_id\":null,"
      "\"login\":\"%s\","
      "\"login_source\":null,"
      "\"password\":\"%s\","
      "\"undelete\":false"
      "}";
  size_t json_size = snprintf(NULL, 0,
                              "{"
                              "\"gift_code_sku_id\":null,"
                              "\"login\":\"%s\","
                              "\"login_source\":null,"
                              "\"password\":\"%s\","
                              "\"undelete\":false"
                              "}",
                              username, password);
  size_t request_size =
      snprintf(NULL, 0, request_fmt, (int)json_size, username, password) + 1;
  char *request_string = malloc(request_size);
  if (!request_string) {
    *error = DISCRUB_ENOMEM;
    return NULL;
  }
  snprintf(request_string, request_size, request_fmt, (int)json_size, username, password);
  enum HTTPError http_error = HTTP_ENOERR;
  struct HTTPResponse *response =
      http_request(connection, request_string, &http_error);
  if (http_error) {
    printf("Error while fetching: %s\n\nWith request: %s\n",
           http_strerror(&http_error), request_string);
    free(request_string);
    *error = DISCRUB_EHTTP;
    return false;
  }
  free(request_string);
  if (!response) {
    printf("Failed to log in: No response\n");
    return NULL;
  }
  if (response->code != 200) {
    printf("Failed to log in: Status code is %hu\n", response->code);
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

  enum JsonError json_error = JSON_ENOERR;
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

  struct JsonToken *token_string = jsontok_get(response_object->as_object, "token");
  struct JsonToken *user_id_string = jsontok_get(response_object->as_object, "user_id");
  if (!token_string || token_string->type != JSON_STRING || !user_id_string || user_id_string->type != JSON_STRING) {
    jsontok_free(response_object);
    *error = DISCRUB_EPARSE;
    return NULL;
  }

  struct LoginResponse *login_response = malloc(sizeof(struct LoginResponse));
  if (!login_response) {
    jsontok_free(response_object);
    *error = DISCRUB_ENOMEM;
    return NULL;
  }
  login_response->token = malloc(strlen(token_string->as_string) + 1);
  if (!login_response->token) {
    free(login_response);
    jsontok_free(response_object);
    *error = DISCRUB_ENOMEM;
    return NULL;
  }
  strcpy(login_response->token, token_string->as_string);

  login_response->user_id = malloc(strlen(user_id_string->as_string) + 1);
  if (!login_response->user_id) {
    free(login_response->token);
    free(login_response);
    jsontok_free(response_object);
    *error = DISCRUB_ENOMEM;
    return NULL;
  }
  strcpy(login_response->user_id, user_id_string->as_string);

  return login_response;
}

const char *discrub_strerror(enum DiscrubError *error) {
  switch (*error) {
    case DISCRUB_ENOMEM: return "Memory allocation failed";
    case DISCRUB_EARGS: return "Invalid arguments provided";
    case DISCRUB_EHTTP: return "HTTP request failed";
    default: return "Unknown Discrub error";
  }
}
