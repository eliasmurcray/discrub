#include "cli/cli.h"
#include "net/err.h"
#include "net/ratelimit.h"
#include "net/ssl.h"
#include <openssl/ssl.h>
#include <sodium.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef int (*cmd_fn)(SSL *ssl, int argc, char **argv);

typedef struct {
    const char *name;
    cmd_fn fn;
    bool needs_network;
} cli_command_t;

static const cli_command_t commands[] = {
    {"login", cmd_login, true},
    {"whoami", cmd_whoami, true},
    {"guilds", cmd_guilds, true},
    {"logout", cmd_logout, false},
};

int main(int argc, char **argv) {
    if (sodium_init() == -1) {
        fprintf(stderr, "failed to initialize sodium");
        return 1;
    }
    if (net_init() != 0) {
        fprintf(stderr, "failed to initialize networking: %s\n",
                net_strerror(net_errno));
        return 1;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: xcord <command> [args]\ncommands:\n");
        for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
            fprintf(stderr, "  %s\n", commands[i].name);
        }
        net_shutdown();
        return 1;
    }
    const cli_command_t *matched = NULL;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(argv[1], commands[i].name) == 0) {
            matched = &commands[i];
            break;
        }
    }
    if (!matched) {
        fprintf(stderr, "unknown command: %s\n", argv[1]);
        net_shutdown();
        return 1;
    }
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    if (matched->needs_network) {
        ctx = ssl_ctx_new();
        if (!ctx) {
            fprintf(stderr, "failed to create ssl context: %s\n",
                    net_strerror(net_errno));
            net_shutdown();
            return 1;
        }
        ssl = ssl_connect(ctx, "discord.com", 443);
        if (!ssl) {
            fprintf(stderr, "failed to connect: %s\n", net_strerror(net_errno));
            SSL_CTX_free(ctx);
            net_shutdown();
            return 1;
        }
    }
    int result = matched->fn(ssl, argc - 1, argv + 1);
    ratelimit_cleanup();
    if (ssl) {
        ssl_disconnect(ssl);
    }
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    net_shutdown();
    return result;
}
