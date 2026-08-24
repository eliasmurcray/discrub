#include "ratelimit.h"
#include "common/strutil.h"
#include <errno.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#define BASE_DELAY_MS 150
#define BASE_JITTER_MS 150

typedef struct {
    char *key;
    long remaining;
    long long reset_at_ms;
} bucket_state_t;

typedef struct {
    char *route;
    char *bucket;
} route_bucket_t;

static bucket_state_t *states = NULL;
static size_t states_len = 0;
static size_t states_cap = 0;

static route_bucket_t *routes = NULL;
static size_t routes_len = 0;
static size_t routes_cap = 0;

static long long now_ms(void) {
#if defined(_WIN32)
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static void sleep_ms(long ms) {
    if (ms <= 0) {
        return;
    }
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    struct timespec remaining;
    while (nanosleep(&ts, &remaining) == -1 && errno == EINTR) {
        ts = remaining;
    }
#endif
}

static long jitter_ms(long max_ms) {
    if (max_ms <= 0) {
        return 0;
    }
    unsigned char buf[4];
    if (RAND_bytes(buf, sizeof(buf)) != 1) {
        return max_ms / 2;
    }
    uint32_t value = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                     ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    return (long)(value % (uint32_t)(max_ms + 1));
}

static int ensure_cap(char **buf, size_t *cap, size_t needed) {
    if (needed <= *cap) {
        return 0;
    }
    size_t new_cap = *cap ? *cap : 16;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char *tmp = realloc(*buf, new_cap);
    if (!tmp) {
        return -1;
    }
    *buf = tmp;
    *cap = new_cap;
    return 0;
}

static char *normalize_route(const char *method, const char *path) {
    size_t cap = 0;
    size_t len = 0;
    char *out = NULL;
    size_t i = 0;
    while (path[i] && path[i] != '?') {
        if (path[i] == '/') {
            if (ensure_cap(&out, &cap, len + 2) != 0) {
                free(out);
                return NULL;
            }
            out[len++] = '/';
            i++;
            continue;
        }
        size_t seg_start = i;
        while (path[i] && path[i] != '/' && path[i] != '?') {
            i++;
        }
        size_t seg_len = i - seg_start;
        int all_digits = 1;
        for (size_t j = 0; j < seg_len; j++) {
            if (path[seg_start + j] < '0' || path[seg_start + j] > '9') {
                all_digits = 0;
                break;
            }
        }
        size_t needed = all_digits ? 2 : seg_len;
        if (ensure_cap(&out, &cap, len + needed + 1) != 0) {
            free(out);
            return NULL;
        }
        if (all_digits) {
            out[len++] = 'i';
            out[len++] = 'd';
        } else {
            memcpy(out + len, path + seg_start, seg_len);
            len += seg_len;
        }
    }
    if (ensure_cap(&out, &cap, len + 1) != 0) {
        free(out);
        return NULL;
    }
    out[len] = '\0';
    size_t route_cap = strlen(method) + 1 + len + 1;
    char *route = malloc(route_cap);
    if (!route) {
        free(out);
        return NULL;
    }
    int written = snprintf(route, route_cap, "%s %s", method, out);
    free(out);
    if (written < 0 || (size_t)written >= route_cap) {
        free(route);
        return NULL;
    }
    return route;
}

static bucket_state_t *find_or_create_state(const char *key) {
    for (size_t i = 0; i < states_len; i++) {
        if (strcmp(states[i].key, key) == 0) {
            return &states[i];
        }
    }
    if (states_len >= states_cap) {
        size_t new_cap = states_cap ? states_cap * 2 : 8;
        bucket_state_t *tmp = realloc(states, new_cap * sizeof(bucket_state_t));
        if (!tmp) {
            return NULL;
        }
        states = tmp;
        states_cap = new_cap;
    }
    char *key_copy = dup_str(key);
    if (!key_copy) {
        return NULL;
    }
    bucket_state_t *entry = &states[states_len];
    entry->key = key_copy;
    entry->remaining = -1;
    entry->reset_at_ms = 0;
    states_len++;
    return entry;
}

static const char *find_bucket_for_route(const char *route) {
    for (size_t i = 0; i < routes_len; i++) {
        if (strcmp(routes[i].route, route) == 0) {
            return routes[i].bucket;
        }
    }
    return NULL;
}

static int record_bucket_for_route(const char *route, const char *bucket) {
    for (size_t i = 0; i < routes_len; i++) {
        if (strcmp(routes[i].route, route) == 0) {
            char *bucket_copy = dup_str(bucket);
            if (!bucket_copy) {
                return -1;
            }
            free(routes[i].bucket);
            routes[i].bucket = bucket_copy;
            return 0;
        }
    }
    if (routes_len >= routes_cap) {
        size_t new_cap = routes_cap ? routes_cap * 2 : 8;
        route_bucket_t *tmp = realloc(routes, new_cap * sizeof(route_bucket_t));
        if (!tmp) {
            return -1;
        }
        routes = tmp;
        routes_cap = new_cap;
    }
    char *route_copy = dup_str(route);
    if (!route_copy) {
        return -1;
    }
    char *bucket_copy = dup_str(bucket);
    if (!bucket_copy) {
        free(route_copy);
        return -1;
    }
    routes[routes_len].route = route_copy;
    routes[routes_len].bucket = bucket_copy;
    routes_len++;
    return 0;
}

void ratelimit_wait(const char *method, const char *path) {
    char *route = normalize_route(method, path);
    if (!route) {
        sleep_ms(BASE_DELAY_MS + jitter_ms(BASE_JITTER_MS));
        return;
    }
    const char *bucket = find_bucket_for_route(route);
    const char *key = bucket ? bucket : route;
    bucket_state_t *state = find_or_create_state(key);
    if (state && state->remaining == 0) {
        long long left = state->reset_at_ms - now_ms();
        if (left > 0) {
            sleep_ms((long)left + jitter_ms(150));
        }
    }
    free(route);
    sleep_ms(BASE_DELAY_MS + jitter_ms(BASE_JITTER_MS));
}

void ratelimit_update(const char *method, const char *path,
                      const http_response_t *resp) {
    if (!resp) {
        return;
    }
    char *route = normalize_route(method, path);
    if (!route) {
        return;
    }
    const char *key = route;
    if (resp->bucket && record_bucket_for_route(route, resp->bucket) == 0) {
        key = resp->bucket;
    }
    bucket_state_t *state = find_or_create_state(key);
    free(route);
    if (!state) {
        return;
    }
    if (resp->status == 429) {
        state->remaining = 0;
        long retry_ms = resp->retry_after_ms > 0 ? resp->retry_after_ms : 1000;
        state->reset_at_ms = now_ms() + retry_ms;
        return;
    }
    if (resp->rl_remaining >= 0) {
        state->remaining = resp->rl_remaining;
        long reset_ms = resp->rl_reset_ms > 0 ? resp->rl_reset_ms : 0;
        state->reset_at_ms = now_ms() + reset_ms;
    }
}

void ratelimit_cleanup(void) {
    for (size_t i = 0; i < states_len; i++) {
        free(states[i].key);
    }
    free(states);
    states = NULL;
    states_len = 0;
    states_cap = 0;
    for (size_t i = 0; i < routes_len; i++) {
        free(routes[i].route);
        free(routes[i].bucket);
    }
    free(routes);
    routes = NULL;
    routes_len = 0;
    routes_cap = 0;
}
