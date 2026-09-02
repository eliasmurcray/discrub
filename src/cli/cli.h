#pragma once
#include <openssl/ssl.h>

char *cli_load_token(void);
int cli_credentials_exist(void);

int cmd_login(SSL *ssl, int argc, char **argv);
int cmd_whoami(SSL *ssl, int argc, char **argv);
int cmd_guilds(SSL *ssl, int argc, char **argv);
int cmd_logout(SSL *ssl, int argc, char **argv);
int cmd_purge(SSL *ssl, int argc, char **argv);
int cmd_channels(SSL *ssl, int argc, char **argv);
