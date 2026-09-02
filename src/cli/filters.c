#include "filters.h"
#include "scope.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DISCORD_EPOCH_MS 1420070400000LL

static int parse_duration_ms(const char *s, long long *out_ms) {
    if (!s || !*s) {
        return 0;
    }
    char *end = NULL;
    double value = strtod(s, &end);
    if (end == s || value < 0) {
        return 0;
    }
    long long unit_ms;
    switch (*end) {
    case 's':
        unit_ms = 1000;
        break;
    case 'm':
        unit_ms = 60LL * 1000;
        break;
    case 'h':
        unit_ms = 3600LL * 1000;
        break;
    case 'd':
        unit_ms = 86400LL * 1000;
        break;
    case 'w':
        unit_ms = 7LL * 86400 * 1000;
        break;
    default:
        return 0;
    }
    *out_ms = (long long)(value * (double)unit_ms);
    return 1;
}

static char *snowflake_for_cutoff(long long ms_ago) {
    long long now_ms = (long long)time(NULL) * 1000;
    long long cutoff_unix_ms = now_ms - ms_ago;
    long long cutoff_discord_ms = cutoff_unix_ms - DISCORD_EPOCH_MS;
    if (cutoff_discord_ms < 0) {
        cutoff_discord_ms = 0;
    }
    unsigned long long snowflake = (unsigned long long)cutoff_discord_ms << 22;
    int needed = snprintf(NULL, 0, "%llu", snowflake);
    if (needed < 0) {
        return NULL;
    }
    char *out = malloc((size_t)needed + 1);
    if (!out) {
        return NULL;
    }
    snprintf(out, (size_t)needed + 1, "%llu", snowflake);
    return out;
}

static char **split_csv(const char *s, size_t *out_count) {
    size_t count = 1;
    for (const char *p = s; *p; p++) {
        if (*p == ',') {
            count++;
        }
    }
    char **tokens = malloc(count * sizeof(char *));
    if (!tokens) {
        return NULL;
    }
    size_t idx = 0;
    const char *start = s;
    for (const char *p = s;; p++) {
        if (*p == ',' || *p == '\0') {
            size_t len = (size_t)(p - start);
            char *tok = malloc(len + 1);
            if (!tok) {
                for (size_t i = 0; i < idx; i++) {
                    free(tokens[i]);
                }
                free(tokens);
                return NULL;
            }
            memcpy(tok, start, len);
            tok[len] = '\0';
            tokens[idx++] = tok;
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }
    *out_count = idx;
    return tokens;
}

bool filters_build(const char *content, const char *author_id, const char *has,
                   const char *older_than, const char *newer_than, int offset,
                   BuiltFilters *out) {
    memset(out, 0, sizeof(*out));
    if (author_id) {
        size_t count = 0;
        char **tokens = split_csv(author_id, &count);
        if (!tokens) {
            fprintf(stderr, "out of memory parsing --author\n");
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            if (!scope_is_snowflake(tokens[i])) {
                fprintf(stderr,
                        "--author must be a numeric user id (invalid: %s)\n",
                        tokens[i]);
                for (size_t j = 0; j < count; j++) {
                    free(tokens[j]);
                }
                free(tokens);
                return false;
            }
        }
        out->author_ids_owned = tokens;
        out->author_id_count_owned = count;
        out->params.author_ids = (const char *const *)tokens;
        out->params.author_id_count = count;
    }
    out->params.content = content;
    out->params.has = has;
    out->params.offset = offset;
    out->params.include_nsfw = true;
    if (older_than) {
        long long ms;
        if (!parse_duration_ms(older_than, &ms)) {
            fprintf(
                stderr,
                "invalid duration for --older-than: %s (try 30d, 12h, 90m)\n",
                older_than);
            return false;
        }
        out->max_id_owned = snowflake_for_cutoff(ms);
        if (!out->max_id_owned) {
            return false;
        }
        out->params.max_id = out->max_id_owned;
    }
    if (newer_than) {
        long long ms;
        if (!parse_duration_ms(newer_than, &ms)) {
            fprintf(stderr,
                    "invalid duration for --newer-than: %s (try 7d, 24h)\n",
                    newer_than);
            free(out->max_id_owned);
            out->max_id_owned = NULL;
            return false;
        }
        out->min_id_owned = snowflake_for_cutoff(ms);
        if (!out->min_id_owned) {
            free(out->max_id_owned);
            out->max_id_owned = NULL;
            return false;
        }
        out->params.min_id = out->min_id_owned;
    }
    return true;
}

void filters_free(BuiltFilters *filters) {
    for (size_t i = 0; i < filters->author_id_count_owned; i++) {
        free(filters->author_ids_owned[i]);
    }
    free(filters->author_ids_owned);
    filters->author_ids_owned = NULL;
    filters->author_id_count_owned = 0;
    free(filters->max_id_owned);
    free(filters->min_id_owned);
    filters->max_id_owned = NULL;
    filters->min_id_owned = NULL;
}
