#pragma once
#include "discord/discord.h"
#include <stdbool.h>

typedef struct {
    DiscordSearchParams params;
    char **author_ids_owned;
    size_t author_id_count_owned;
    char *max_id_owned;
    char *min_id_owned;
} BuiltFilters;

bool filters_build(const char *content, const char *author_id, const char *has, const char *older_than, const char *newer_than, int offset, BuiltFilters *out);
void filters_free(BuiltFilters *filters);
