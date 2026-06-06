#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "app_context.h"
#include "window.h"

/* Handle raw window events for input (Keyboard, Text) */
void input_handle_event(AppContext *ctx, WindowEvent *evt);

#endif /* INPUT_HANDLER_H */
