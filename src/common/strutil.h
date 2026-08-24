#pragma once
#include <stddef.h>

char *dup_str(const char *s);
int ci_eq_n(const char *a, const char *b, size_t n);
int str_eq_ci(const char *a, const char *b);
int str_contains_ci(const char *haystack, const char *needle);
void *find_bytes(const void *haystack, size_t haystack_len, const void *needle, size_t needle_len);
