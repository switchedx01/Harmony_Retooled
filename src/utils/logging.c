#include "logging.h"
#include <stdarg.h>
#include <string.h>

static FILE *log_file = NULL;

Result logging_init(const char *log_file_path) {
  if (!c_assert(log_file_path != NULL)) {
    return RESULT_ERROR_NULL_POINTER;
  }

  if (log_file != NULL) {
    /* Already initialized */
    return RESULT_SUCCESS;
  }

  log_file = fopen(log_file_path, "a");
  if (!c_assert(log_file != NULL)) {
    perror("Failed to open log file");
    return RESULT_ERROR_FILE_IO;
  }

  log_message("INFO", "Logging initialized.");
  return RESULT_SUCCESS;
}

void logging_shutdown(void) {
  if (log_file != NULL) {
    log_message("INFO", "Logging shutdown.");
    fclose(log_file);
    log_file = NULL;
  }
}

void log_message(const char *level, const char *message) {
  /* P10 Rule 7: Check return values. fprintf returns char count. */
  /* We attempt to log to stdout and file. */

  if (level == NULL)
    level = "UNKNOWN";
  if (message == NULL)
    message = "(null)";

  if (log_file != NULL) {
    (void)fprintf(log_file, "[%s] %s\n", level,
                  message); /* Explicit void cast per P10 Rule 7 Rationale */
    (void)fflush(log_file);
  }

  (void)fprintf(stdout, "[%s] %s\n", level, message);
}

void tst_debugging(const char *format, const char *file, int line,
                   const char *expr) {
  /* Used by c_assert macro */
  if (log_file != NULL) {
    (void)fprintf(log_file, "ASSERTION FAILED: %s at %s:%d (%s)\n", format,
                  file, line, expr);
    (void)fflush(log_file);
  }
  (void)fprintf(stderr, "ASSERTION FAILED: %s at %s:%d (%s)\n", format, file,
                line, expr);
}
