#include "discrub_interface.h"
#include "openssl_helpers.h"

static int assert_snowflake(const char* snowflake_id) {
  if (!snowflake_id) return 1;
  errno = 0;
  char *endptr = NULL;
  strtoull(snowflake_id, &endptr, 10);
  return !errno && *endptr != '\0';
}

int discrub_delete_message(BIO *bio, const char* guild_id, const char* message_id, char **error) {
  if (!bio) {
    *error = "Invalid BIO object";
    return 1;
  }
  if (assert_snowflake(guild_id)) {
    *error = "Invalid guild_id";
    return 1;
  }
  if (assert_snowflake(message_id)) {
    *error = "Invalid message_id";
    return 1;
  }
  return 0;
}
