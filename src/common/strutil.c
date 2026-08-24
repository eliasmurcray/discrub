#include "strutil.h"
#include <stdlib.h>
#include <string.h>

char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, n);
    return copy;
}

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

int ci_eq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (to_lower_ascii(a[i]) != to_lower_ascii(b[i])) {
            return 0;
        }
    }
    return 1;
}

int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (to_lower_ascii(*a) != to_lower_ascii(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int str_contains_ci(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < needle_len && h[i] &&
               to_lower_ascii(h[i]) == to_lower_ascii(needle[i])) {
            i++;
        }
        if (i == needle_len) {
            return 1;
        }
    }
    return 0;
}

void *find_bytes(const void *haystack, size_t haystack_len, const void *needle,
                 size_t needle_len) {
    if (needle_len == 0 || needle_len > haystack_len) {
        return NULL;
    }
    const unsigned char *h = haystack;
    const unsigned char *n = needle;
    size_t last = haystack_len - needle_len;
    for (size_t i = 0; i <= last; i++) {
        if (memcmp(h + i, n, needle_len) == 0) {
            return (void *)(h + i);
        }
    }
    return NULL;
}
