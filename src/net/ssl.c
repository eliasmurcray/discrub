#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "err.h"
#include "ssl.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

int net_init(void) {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        net_errno = NET_ECTX;
        return -1;
    }
#endif
    return 0;
}

void net_shutdown(void) {
#if defined(_WIN32)
    WSACleanup();
#endif
}

SSL_CTX *ssl_ctx_new(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        net_errno = NET_ECTX;
        return NULL;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        net_errno = NET_ECTX;
        SSL_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

SSL *ssl_connect(SSL_CTX *ctx, const char *hostname, uint16_t port) {
    if (!ctx || !hostname) {
        net_errno = NET_EARGS;
        return NULL;
    }
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned int)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *results;
    if (getaddrinfo(hostname, port_str, &hints, &results) != 0) {
        net_errno = NET_ERESOLVE;
        return NULL;
    }
#if defined(_WIN32)
    SOCKET sockfd = INVALID_SOCKET;
#else
    int sockfd = -1;
#endif
    for (struct addrinfo *addr = results; addr; addr = addr->ai_next) {
        sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
#if defined(_WIN32)
        if (sockfd == INVALID_SOCKET) {
            continue;
        }
#else
        if (sockfd < 0) {
            continue;
        }
#endif
        if (connect(sockfd, addr->ai_addr, addr->ai_addrlen) == 0) {
            break;
        }
#if defined(_WIN32)
        closesocket(sockfd);
        sockfd = INVALID_SOCKET;
#else
        close(sockfd);
        sockfd = -1;
#endif
    }
    freeaddrinfo(results);
#if defined(_WIN32)
    if (sockfd == INVALID_SOCKET) {
        net_errno = NET_ECONNECT;
        return NULL;
    }
#else
    if (sockfd < 0) {
        net_errno = NET_ECONNECT;
        return NULL;
    }
#endif
    struct timeval tv = {.tv_sec = 30, .tv_usec = 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag,
                   sizeof(flag)) != 0) {
        net_errno = NET_ESOCK;
        goto fail_sock;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&flag,
                   sizeof(flag)) != 0) {
        net_errno = NET_ESOCK;
        goto fail_sock;
    }
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        net_errno = NET_ESSL;
        goto fail_sock;
    }
    SSL_set_fd(ssl, (int)sockfd);
    if (!SSL_set_tlsext_host_name(ssl, hostname)) {
        net_errno = NET_ESSL;
        goto fail_ssl;
    }
    X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(param,
                                    X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (!X509_VERIFY_PARAM_set1_host(param, hostname, 0)) {
        net_errno = NET_ESSL;
        goto fail_ssl;
    }
    if (SSL_connect(ssl) <= 0) {
        net_errno = NET_EHANDSHAKE;
        goto fail_ssl;
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        net_errno = NET_EVERIFY;
        SSL_shutdown(ssl);
        goto fail_ssl;
    }
    return ssl;
fail_ssl:
    SSL_free(ssl);
fail_sock:
#if defined(_WIN32)
    closesocket(sockfd);
#else
    close(sockfd);
#endif
    return NULL;
}

void ssl_disconnect(SSL *ssl) {
#if defined(_WIN32)
    SOCKET sockfd = (SOCKET)SSL_get_fd(ssl);
#else
    int sockfd = SSL_get_fd(ssl);
#endif
    SSL_shutdown(ssl);
    SSL_free(ssl);
#if defined(_WIN32)
    closesocket(sockfd);
#else
    close(sockfd);
#endif
}
