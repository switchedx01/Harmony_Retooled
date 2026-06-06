#ifndef LOGGING_H
#define LOGGING_H

#include "common.h"

/* Initialize logging subsystem */
Result logging_init(const char *log_file_path);

/* Shutdown logging subsystem */
void logging_shutdown(void);

/* Log a message */
void log_message(const char *level, const char *message);

/* Implementation of tst_debugging for c_assert */
void tst_debugging(const char *format, const char *file, int line,
                   const char *expr);

#endif /* LOGGING_H */
