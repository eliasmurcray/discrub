#include <openssl/ssl.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h>
#else
#include <unistd.h>
#endif

#include "discrub_interface.h"
#include "input_helpers.h"

/* Credit to @Bernardo Ramos https://stackoverflow.com/a/28827188/20918291 */
void sleep_ms(int milliseconds) {
#ifdef WIN32
  Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
#else
  if (milliseconds >= 1000)
    sleep(milliseconds / 1000);
  usleep((milliseconds % 1000) * 1000);
#endif
}

static char *allocate_string(const char *source) {
  if (!source) return NULL;
  char *dest = malloc(strlen(source) + 1);
  if (!dest) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(1);
  }
  strcpy(dest, source);
  return dest;
}

int main() {
  struct SearchOptions search_options;
  char *options_string = load_file_as_string("options.json");
  if (!options_string) {
    fprintf(stderr, "Please set options in options.json to use this script.\n");
    return 1;
  }
  enum JsonError json_error = JSON_ENOERR;
  struct JsonToken *options_object = jsontok_parse(options_string, &json_error);
  if (json_error) {
    fprintf(stderr, "Failed to parse options.json: %s\n", jsontok_strerror(json_error));
    free(options_string);
    return 1;
  }
  if (options_object->type != JSON_OBJECT) {
    fprintf(stderr, "Error in options.json: Not a valid JSON object\n");
    free(options_string);
    return 1;
  }
  struct JsonToken *server_id_string = jsontok_get(options_object->as_object, "server_id");
  if (!server_id_string || server_id_string->type != JSON_STRING) {
    fprintf(stderr, "Error in options.json: 'server_id' is a required key\n");
    free(options_string);
    jsontok_free(options_object);
    return 1;
  }
  char *server_id = allocate_string(server_id_string->as_string);
  struct JsonToken *limit_number = jsontok_get(options_object->as_object, "limit");
  if (!limit_number || limit_number->type != JSON_NUMBER) {
    fprintf(stderr, "Error in options.json: 'limit' is a required key\n");
    free(server_id);
    free(options_string);
    jsontok_free(options_object);
    return 1;
  }
  if (limit_number->as_number < 0) {
    fprintf(stderr, "Error in options.json: 'limit' cannot be negative\n");
    free(server_id);
    free(options_string);
    jsontok_free(options_object);
    return 1;
  }
  size_t limit = limit_number->as_number;
  struct JsonToken *channel_id_string = jsontok_get(options_object->as_object, "channel_id");
  if (!channel_id_string || channel_id_string->type != JSON_STRING) {
    fprintf(stderr, "Error in options.json: 'channel_id' is a required key\n");
    free(server_id);
    free(options_string);
    jsontok_free(options_object);
    return 1;
  }
  search_options.channel_id = allocate_string(channel_id_string->as_string);
  struct JsonToken *include_nsfw_boolean = jsontok_get(options_object->as_object, "include_nsfw");
  if (include_nsfw_boolean && include_nsfw_boolean->type == JSON_BOOLEAN) {
    search_options.include_nsfw = include_nsfw_boolean->as_boolean;
  }
  struct JsonToken *content_string = jsontok_get(options_object->as_object, "content");
  if (content_string && content_string->type == JSON_STRING) {
    search_options.content = allocate_string(content_string->as_string);
  }
  struct JsonToken *mentions_string = jsontok_get(options_object->as_object, "mentions");
  if (mentions_string && mentions_string->type == JSON_STRING) {
    search_options.mentions = allocate_string(mentions_string->as_string);
  }
  struct JsonToken *pinned_boolean = jsontok_get(options_object->as_object, "pinned");
  if (pinned_boolean && pinned_boolean->type == JSON_BOOLEAN) {
    search_options.pinned = pinned_boolean->as_boolean;
  }
  jsontok_free(options_object);
  free(options_string);

  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
  if (ctx == NULL) {
    fprintf(stderr, "Failed to initialize SSL context\n");
    return 1;
  }

  BIO *connection = BIO_new_ssl_connect(ctx);
  if (!connection) {
    fprintf(stderr, "Failed to create SSL connection\n");
    SSL_CTX_free(ctx);
    return 1;
  }

  BIO_set_conn_hostname(connection, "discord.com:443");

  if (BIO_do_connect(connection) <= 0) {
    fprintf(stderr, "Failed connection\n");
    BIO_free_all(connection);
    SSL_CTX_free(ctx);
    return 1;
  }

  char *cache = load_file_as_string(".discrub_cache"), *password = NULL;
  struct LoginResponse *login_response = NULL;
  enum DiscrubError error = DISCRUB_ENOERR;
  if (cache) {
    login_response = malloc(sizeof(struct LoginResponse));
    char *token = strtok(cache, ";");
    login_response->token = token;
    login_response->user_id = strtok(NULL, ";");
  } else {
    printf("Enter username: ");
    char username[321];
    scanf("%320s", username);
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    printf("Enter password: ");
    password = get_password();

    struct LoginResponse *login_response = discrub_login(connection, username, password, &error);
    if (!login_response) {
      fprintf(stderr, "Login failed: %s\n", discrub_strerror(&error));
      free(server_id);
      free(password);
      BIO_free_all(connection);
      SSL_CTX_free(ctx);
      EVP_cleanup();
      ERR_free_strings();
      return 1;
    }
    FILE *file = fopen(".discrub_cache", "w");
    if (file) {
      fprintf(file, "%s;%s", login_response->token, login_response->user_id);
      fclose(file);
    }
  }
  printf("\nLogged in successfully.\n");
  search_options.author_id = login_response->user_id;

  size_t message_count = 0;
  struct DiscordMessage *messages = NULL;
  while (message_count < limit) {
    search_options.offset = message_count;
    struct SearchResponse *search_response = discrub_search(connection, login_response->token, server_id, &search_options, &error);
    if (!search_response) {
      size_t i = 0;
      for (; i < message_count; i++) {
        struct DiscordMessage *message = &messages[i];
        free(message->id);
        free(message->content);
        free(message->timestamp);
        free(message->author_id);
        free(message->author_username);
      }
      free(messages);
      free(server_id);
      free(password);
      BIO_free_all(connection);
      SSL_CTX_free(ctx);
      EVP_cleanup();
      ERR_free_strings();
      fprintf(stderr, "Failed to search: %s\n", discrub_strerror(&error));
      return 1;
    }
    message_count += search_response->length;
    if (search_response->length == 0) {
      break;
    }
    struct DiscordMessage *new_messages = realloc(messages, message_count * sizeof(struct DiscordMessage));
    if (!new_messages) {
      size_t i = 0;
      for (; i < message_count; i++) {
        struct DiscordMessage *message = &messages[i];
        free(message->id);
        free(message->content);
        free(message->timestamp);
        free(message->author_id);
        free(message->author_username);
      }
      free(messages);
      free(server_id);
      free(password);
      BIO_free_all(connection);
      SSL_CTX_free(ctx);
      EVP_cleanup();
      ERR_free_strings();
      fprintf(stderr, "Failed to search: Out of memory\n");
      return 1;
    }
    messages = new_messages;
    size_t i = 0;
    for (; i < search_response->length; i++) {
      messages[message_count - search_response->length + i] = search_response->messages[i];
    }
    free(search_response->messages);
    free(search_response);
    printf("\rFetched %zu/%zu messages...", message_count, limit);
    fflush(stdout);
    sleep_ms(1500);
  }
  printf("\rFetched all messages successfully.\n");
  printf("\nDeleting messages...\n\n");

  size_t i = 0;
  for (; i < message_count; i++) {
    struct DiscordMessage message = messages[i];
    printf("Deleting message %s...\n[%s] %s: %s\n", message.id, message.timestamp, message.author_username,
           message.content);
    if (discrub_delete_message(connection, login_response->token, search_options.channel_id, message.id, &error)) {
      fprintf(stderr, "Failed to delete message %s: %s\n", message.id, discrub_strerror(&error));
      break;
    }
    printf("Deleted message %s successfully.\n\n", message.id);
    sleep_ms(1500);
  }

  for (i = 0; i < message_count; i++) {
    struct DiscordMessage *message = &messages[i];
    free(message->id);
    free(message->content);
    free(message->timestamp);
    free(message->author_id);
    free(message->author_username);
  }
  free(messages);
  free(server_id);
  free(password);
  BIO_free_all(connection);
  SSL_CTX_free(ctx);
  EVP_cleanup();
  ERR_free_strings();
}
