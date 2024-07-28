#include "discrub_interface.h"
#include "openssl_helpers.h"

static int assert_snowflake(const char* snowflake_id) {
  if (!snowflake_id) return 1;
  errno = 0;
  char *endptr = NULL;
  strtoull(snowflake_id, &endptr, 10);
  return !errno && *endptr != '\0';
}

int discrub_delete_message(BIO *bio, const char* channel_id, const char* message_id, const char* token, char **error) {
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
  char *request_string = (char *)malloc(request_size);
  if (0 > snprintf(request_string, request_size, request_fmt, channel_id, message_id, token)) {
    *error = "Failed to format request string";
    return 1;
  }
  char *response_string = send_request(bio, request_string, error);
  free(request_string);
  if (!response_string) return 1;
  struct HTTPResponse *response = parse_response(response_string, error);
  if (!response) return 1;
  if (response->code != 204) {
    int error_size = snprintf(NULL, 0, "Status code is %hu", response->code) + 1;
    char* buffer = (char *)malloc(error_size);
    snprintf(buffer, error_size, "Status code is %hu", response->code);
    *error = buffer;
    return 1;
  }
  return 0;
}
