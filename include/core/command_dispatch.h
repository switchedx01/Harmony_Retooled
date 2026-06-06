#ifndef COMMAND_DISPATCH_H
#define COMMAND_DISPATCH_H

#include "app_context.h"

/*
 * Dispatch a string-based command from the UI.
 * Returns true if the command was handled.
 */
bool dispatch_command(AppContext *ctx, const char *cmd, int mx_context);

#endif /* COMMAND_DISPATCH_H */
