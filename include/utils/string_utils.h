#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include "common.h"
#include <stddef.h>

/* Safe string copy. Ensures null-termination. Returns error if truncated. */
Result safe_strncpy(char *dest, const char *src, size_t dest_size);

/* Safe string concatenation. */
Result safe_strncat(char *dest, const char *src, size_t dest_size);

/* Safe string length (handles null pointer). */
size_t safe_strnlen(const char *str, size_t max_len);

#endif /* STRING_UTILS_H */
