#include "http.h"
#include "common/strutil.h"
#include "err.h"
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_HDR_CAP 8192
#define INITIAL_BODY_CAP 16384
#define MIN_CHUNK_CAP 16384

typedef struct {
    int status;
    long retry_after_ms;
    long rl_remaining;
    long rl_reset_ms;
    char *bucket;
    bool close;
    bool chunked;
    long content_len;
} hdr_info_t;

static int ssl_write_all(SSL *ssl, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = SSL_write(ssl, data + sent, (int)(len - sent));
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (err == SSL_ERROR_SYSCALL &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            net_errno = NET_ETIMEOUT;
        } else {
            net_errno = NET_EWRITE;
        }
        return -1;
    }
    return 0;
}

static int ssl_read_some(SSL *ssl, char *buf, int max_len) {
    for (;;) {
        int n = SSL_read(ssl, buf, max_len);
        if (n > 0) {
            return n;
        }
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            return 0;
        }
        if (err == SSL_ERROR_SYSCALL &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            net_errno = NET_ETIMEOUT;
        } else {
            net_errno = NET_EREAD;
        }
        return -1;
    }
}

static long parse_long(const char *val, size_t len) {
    char *tmp = malloc(len + 1);
    if (!tmp) {
        return -1;
    }
    memcpy(tmp, val, len);
    tmp[len] = '\0';
    long result = strtol(tmp, NULL, 10);
    free(tmp);
    return result;
}

static long parse_secs_ms(const char *val, size_t len) {
    char *tmp = malloc(len + 1);
    if (!tmp) {
        return -1;
    }
    memcpy(tmp, val, len);
    tmp[len] = '\0';
    long result = (long)(strtod(tmp, NULL) * 1000.0);
    free(tmp);
    return result;
}

static hdr_info_t parse_headers(const char *hdrs, size_t hdrs_len) {
    hdr_info_t h;
    h.status = -1;
    h.retry_after_ms = -1;
    h.rl_remaining = -1;
    h.rl_reset_ms = -1;
    h.bucket = NULL;
    h.close = false;
    h.chunked = false;
    h.content_len = -1;
    size_t pos = 0;
    bool first = true;
    while (pos < hdrs_len) {
        const char *eol = find_bytes(hdrs + pos, hdrs_len - pos, "\r\n", 2);
        size_t eol_pos = eol ? (size_t)(eol - hdrs) : hdrs_len;
        size_t line_len = eol_pos - pos;
        if (first) {
            const char *sp = memchr(hdrs + pos, ' ', line_len);
            if (sp) {
                h.status = (int)strtol(sp + 1, NULL, 10);
            }
            first = false;
        } else {
            const char *colon = memchr(hdrs + pos, ':', line_len);
            if (colon) {
                const char *name = hdrs + pos;
                size_t name_len = (size_t)(colon - name);
                const char *val = colon + 1;
                const char *line_end = hdrs + eol_pos;
                while (val < line_end && (*val == ' ' || *val == '\t')) {
                    val++;
                }
                size_t val_len = (size_t)(line_end - val);
                if (name_len == sizeof("Retry-After") - 1 &&
                    ci_eq_n(name, "Retry-After", name_len)) {
                    h.retry_after_ms = parse_secs_ms(val, val_len);
                } else if (name_len == sizeof("X-RateLimit-Remaining") - 1 &&
                           ci_eq_n(name, "X-RateLimit-Remaining", name_len)) {
                    h.rl_remaining = parse_long(val, val_len);
                } else if (name_len == sizeof("X-RateLimit-Reset-After") - 1 &&
                           ci_eq_n(name, "X-RateLimit-Reset-After", name_len)) {
                    h.rl_reset_ms = parse_secs_ms(val, val_len);
                } else if (name_len == sizeof("X-RateLimit-Bucket") - 1 &&
                           ci_eq_n(name, "X-RateLimit-Bucket", name_len)) {
                    char *bucket_copy = malloc(val_len + 1);
                    if (bucket_copy) {
                        memcpy(bucket_copy, val, val_len);
                        bucket_copy[val_len] = '\0';
                        free(h.bucket);
                        h.bucket = bucket_copy;
                    }
                } else if (name_len == sizeof("Connection") - 1 &&
                           ci_eq_n(name, "Connection", name_len)) {
                    h.close = val_len == sizeof("close") - 1 &&
                              ci_eq_n(val, "close", val_len);
                } else if (name_len == sizeof("Transfer-Encoding") - 1 &&
                           ci_eq_n(name, "Transfer-Encoding", name_len)) {
                    h.chunked = true;
                } else if (name_len == sizeof("Content-Length") - 1 &&
                           ci_eq_n(name, "Content-Length", name_len)) {
                    h.content_len = parse_long(val, val_len);
                }
            }
        }
        if (!eol) {
            break;
        }
        pos = eol_pos + 2;
    }
    return h;
}

static char *read_chunked(SSL *ssl, char *buf, size_t cap, size_t have) {
    size_t out_len = 0;
    size_t out_cap = INITIAL_BODY_CAP;
    char *out = malloc(out_cap);
    if (!out) {
        net_errno = NET_ENOMEM;
        free(buf);
        return NULL;
    }
    size_t pos = 0;
    for (;;) {
        char *eol;
        while (!(eol = find_bytes(buf + pos, have - pos, "\r\n", 2))) {
            if (have >= cap) {
                cap *= 2;
                char *tmp = realloc(buf, cap);
                if (!tmp) {
                    net_errno = NET_ENOMEM;
                    free(buf);
                    free(out);
                    return NULL;
                }
                buf = tmp;
            }
            int n = ssl_read_some(ssl, buf + have, (int)(cap - have));
            if (n < 0) {
                free(buf);
                free(out);
                return NULL;
            }
            if (n == 0) {
                net_errno = NET_EREAD;
                free(buf);
                free(out);
                return NULL;
            }
            have += (size_t)n;
        }
        size_t chunk_len = strtoul(buf + pos, NULL, 16);
        pos = (size_t)(eol - buf) + 2;
        if (chunk_len == 0) {
            break;
        }
        while (have - pos < chunk_len + 2) {
            if (have >= cap) {
                cap *= 2;
                char *tmp = realloc(buf, cap);
                if (!tmp) {
                    net_errno = NET_ENOMEM;
                    free(buf);
                    free(out);
                    return NULL;
                }
                buf = tmp;
            }
            int n = ssl_read_some(ssl, buf + have, (int)(cap - have));
            if (n < 0) {
                free(buf);
                free(out);
                return NULL;
            }
            if (n == 0) {
                net_errno = NET_EREAD;
                free(buf);
                free(out);
                return NULL;
            }
            have += (size_t)n;
        }
        if (out_len + chunk_len + 1 > out_cap) {
            while (out_len + chunk_len + 1 > out_cap) {
                out_cap *= 2;
            }
            char *tmp = realloc(out, out_cap);
            if (!tmp) {
                net_errno = NET_ENOMEM;
                free(buf);
                free(out);
                return NULL;
            }
            out = tmp;
        }
        memcpy(out + out_len, buf + pos, chunk_len);
        out_len += chunk_len;
        pos += chunk_len + 2;
    }
    out[out_len] = '\0';
    free(buf);
    return out;
}

static char *build_request(const http_request_t *req, size_t *out_len) {
    size_t body_len = req->body ? strlen(req->body) : 0;
    const char *ctype = req->content_type ? req->content_type : "text/plain";
    size_t hdrs_len = 0;
    char *hdrs = NULL;
    if (req->headers) {
        for (int i = 0; req->headers[i]; i++) {
            hdrs_len += strlen(req->headers[i]) + 2;
        }
        hdrs = malloc(hdrs_len + 1);
        if (!hdrs) {
            net_errno = NET_ENOMEM;
            return NULL;
        }
        size_t off = 0;
        for (int i = 0; req->headers[i]; i++) {
            int n = snprintf(hdrs + off, hdrs_len + 1 - off, "%s\r\n",
                             req->headers[i]);
            if (n < 0 || (size_t)n >= hdrs_len + 1 - off) {
                net_errno = NET_ENOMEM;
                free(hdrs);
                return NULL;
            }
            off += (size_t)n;
        }
    }
    size_t cap = strlen(req->method) + strlen(req->path) +
                 strlen(req->hostname) + strlen(ctype) + hdrs_len + body_len +
                 128;
    char *buf = malloc(cap);
    if (!buf) {
        net_errno = NET_ENOMEM;
        free(hdrs);
        return NULL;
    }
    int len = snprintf(buf, cap,
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Content-Type: %s\r\n"
                       "Connection: keep-alive\r\n"
                       "%s"
                       "Content-Length: %zu\r\n"
                       "\r\n"
                       "%s",
                       req->method, req->path, req->hostname, ctype,
                       hdrs ? hdrs : "", body_len, req->body ? req->body : "");
    free(hdrs);
    if (len < 0 || (size_t)len >= cap) {
        net_errno = NET_ENOMEM;
        free(buf);
        return NULL;
    }
    *out_len = (size_t)len;
    return buf;
}

int http_request(SSL *ssl, const http_request_t *req, http_response_t *resp) {
    if (!ssl || !req || !req->method || !req->path || !req->hostname || !resp) {
        net_errno = NET_EARGS;
        return -1;
    }
    memset(resp, 0, sizeof(*resp));
    resp->retry_after_ms = -1;
    resp->rl_remaining = -1;
    resp->rl_reset_ms = -1;
    size_t req_len;
    char *req_buf = build_request(req, &req_len);
    if (!req_buf) {
        return -1;
    }
    int rc = ssl_write_all(ssl, req_buf, req_len);
    free(req_buf);
    if (rc != 0) {
        return -1;
    }
    size_t cap = INITIAL_HDR_CAP;
    char *buf = malloc(cap);
    if (!buf) {
        net_errno = NET_ENOMEM;
        return -1;
    }
    size_t have = 0;
    char *hdr_end = NULL;
    for (;;) {
        hdr_end = have >= 4 ? find_bytes(buf, have, "\r\n\r\n", 4) : NULL;
        if (hdr_end) {
            break;
        }
        if (have >= cap - 1) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                net_errno = NET_ENOMEM;
                free(buf);
                return -1;
            }
            buf = tmp;
        }
        int n = ssl_read_some(ssl, buf + have, (int)(cap - have - 1));
        if (n < 0) {
            free(buf);
            return -1;
        }
        if (n == 0) {
            net_errno = NET_EREAD;
            free(buf);
            return -1;
        }
        have += (size_t)n;
    }
    size_t hdrs_len = (size_t)(hdr_end - buf);
    char *body = hdr_end + 4;
    size_t extra = have - hdrs_len - 4;
    hdr_info_t h = parse_headers(buf, hdrs_len);
    if (h.status < 0) {
        net_errno = NET_EHDRS;
        free(h.bucket);
        free(buf);
        return -1;
    }
    resp->status = h.status;
    resp->retry_after_ms = h.retry_after_ms;
    resp->rl_remaining = h.rl_remaining;
    resp->rl_reset_ms = h.rl_reset_ms;
    resp->bucket = h.bucket;
    resp->should_close = h.close;
    if (h.chunked) {
        size_t chunk_cap = extra > MIN_CHUNK_CAP ? extra : MIN_CHUNK_CAP;
        char *chunk_buf = malloc(chunk_cap);
        if (!chunk_buf) {
            net_errno = NET_ENOMEM;
            free(buf);
            return -1;
        }
        memcpy(chunk_buf, body, extra);
        free(buf);
        char *dec = read_chunked(ssl, chunk_buf, chunk_cap, extra);
        if (!dec) {
            return -1;
        }
        if (dec[0] == '\0') {
            free(dec);
            resp->body = NULL;
            resp->body_len = 0;
        } else {
            resp->body = dec;
            resp->body_len = strlen(dec);
        }
        return 0;
    }
    if (h.content_len <= 0) {
        free(buf);
        resp->body = NULL;
        resp->body_len = 0;
        return 0;
    }
    size_t content_len = (size_t)h.content_len;
    char *out = malloc(content_len + 1);
    if (!out) {
        net_errno = NET_ENOMEM;
        free(buf);
        return -1;
    }
    size_t copy_len = extra < content_len ? extra : content_len;
    memcpy(out, body, copy_len);
    free(buf);
    size_t read_total = copy_len;
    while (read_total < content_len) {
        int n = ssl_read_some(ssl, out + read_total,
                              (int)(content_len - read_total));
        if (n < 0) {
            free(out);
            return -1;
        }
        if (n == 0) {
            net_errno = NET_EREAD;
            free(out);
            return -1;
        }
        read_total += (size_t)n;
    }
    out[content_len] = '\0';
    resp->body = out;
    resp->body_len = content_len;
    return 0;
}
