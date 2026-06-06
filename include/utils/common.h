#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* P10 Rule 8: Limited Preprocessor. Only includes and simple macros. */
/* P10 Rule 5: Assertion Density. Recovery action must be taken. */

/* Debugging function prototype (implemented in logging.c) */
void tst_debugging(const char *format, const char *file, int line,
                   const char *expr);

/* P10 Assertion Macro */
/* Usage: if (!c_assert(condition)) { return ERROR_CODE; } */
#define c_assert(e)                                                            \
  ((e) ? (true)                                                                \
       : (tst_debugging("Assertion failed", __FILE__, __LINE__, #e), false))

/* Standard Return Codes */
typedef enum {
  RESULT_SUCCESS = 0,
  RESULT_ERROR_GENERIC = -1,
  RESULT_ERROR_NULL_POINTER = -2,
  RESULT_ERROR_BUFFER_OVERFLOW = -3,
  RESULT_ERROR_INVALID_PARAMETER = -4,
  RESULT_ERROR_FILE_IO = -5,
  RESULT_ERROR_NOT_IMPLEMENTED = -6
} Result;

/* Max Constraints for Static Allocation (P10 Rule 3) */
#define MAX_PATH_LENGTH 1024
#define MAX_LOG_LINE 1024
#define MAX_PLAYLIST_SIZE 1000
#define MAX_SONG_TITLE 256

/* Layout Constants */
#define SIDEBAR_W 280
#define CONTROL_BAR_H 100

#endif /* COMMON_H */
