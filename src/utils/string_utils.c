#include "string_utils.h"
#include <string.h>

size_t safe_strnlen(const char *str, size_t max_len) {
  size_t len = 0;
  if (str == NULL) {
    return 0;
  }
  while (len < max_len && str[len] != '\0') {
    len++;
  }
  return len;
}

Result safe_strncpy(char *dest, const char *src, size_t dest_size) {
  size_t i;

  if (!c_assert(dest != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (!c_assert(src != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (!c_assert(dest_size > 0))
    return RESULT_ERROR_INVALID_PARAMETER;

  for (i = 0; i < dest_size - 1; i++) {
    if (src[i] == '\0') {
      break;
    }
    dest[i] = src[i];
  }
  dest[i] = '\0';

  if (src[i] != '\0') {
    /* Truncation occurred or src is longer than buffer */
    /* To strictly check truncation, we check if src[i] is effectively null or
     * not */
    return RESULT_ERROR_BUFFER_OVERFLOW;
  }

  return RESULT_SUCCESS;
}

Result safe_strncat(char *dest, const char *src, size_t dest_size) {
  size_t dest_len;
  size_t i;

  if (!c_assert(dest != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (!c_assert(src != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (!c_assert(dest_size > 0))
    return RESULT_ERROR_INVALID_PARAMETER;

  dest_len = safe_strnlen(dest, dest_size);

  if (dest_len >= dest_size - 1) {
    return RESULT_ERROR_BUFFER_OVERFLOW; /* No space to append */
  }

  for (i = 0; i < dest_size - dest_len - 1; i++) {
    if (src[i] == '\0') {
      break;
    }
    dest[dest_len + i] = src[i];
  }
  dest[dest_len + i] = '\0';

  if (src[i] != '\0') {
    return RESULT_ERROR_BUFFER_OVERFLOW;
  }

  return RESULT_SUCCESS;
}
