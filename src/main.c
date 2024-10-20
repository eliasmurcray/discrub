#include <openssl/ssl.h>
#include <stdio.h>

#include "discrub_interface.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <auth token>\n", argv[0]);
    return 1;
  }

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

  enum DiscrubError *error = NULL;
  struct SearchOptions options = {
      .author_id = "1013504042011459594",
      .include_nsfw = 1,
      .channel_id = "1083661464981741588"};
  const char *server_id = "894349669499568148";

  struct SearchResponse *response = discrub_search(connection, argv[1], server_id, &options, error);
  if (response) {
    printf("Response received successfully.\n");
    free(response->messages);
    free(response);
  }

  if (error) {
    fprintf(stderr, "Failed to search messages: %s\n", discrub_strerror(error));
    free(error);
  }

  BIO_free_all(connection);
  SSL_CTX_free(ctx);
  EVP_cleanup();
  ERR_free_strings();
}
