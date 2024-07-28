#include "discrub_interface.h"
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 4) return 1;
  
  SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
  if (ctx == NULL) {
    fprintf(stderr, "Failed to initialize SSL context");
    return 1;
  }

  BIO *bio = BIO_new_ssl_connect(ctx);
  BIO_set_conn_hostname(bio, "discord.com:443");
  if (BIO_do_connect(bio) <= 0) {
    fprintf(stderr, "Failed connection");
    SSL_CTX_free(ctx);
    return 1;
  }

  char *error = NULL;
  if (discrub_delete_message(bio, argv[1], argv[2], argv[3], &error)) {
    fprintf(stderr, "Failed to delete message: %s\n", error);
    return 1;
  }

  free(error);
  SSL_CTX_free(ctx);
  BIO_free(bio);
}
