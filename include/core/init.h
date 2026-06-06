#ifndef INIT_H
#define INIT_H

#include "app_context.h"
#include "common.h"

/* Initialize all subsystems (Log, Window, Audio, DB, UI) */
Result app_init(AppContext *ctx);

/* Shutdown and cleanup */
void app_shutdown(AppContext *ctx);

/* Transition from headless to GUI */
void app_enable_gui(AppContext *ctx);

#endif /* INIT_H */
