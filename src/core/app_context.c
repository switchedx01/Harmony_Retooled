#include "app_context.h"
#include <stddef.h>

/* Dynamically allocate contexts to avoid .bss size/overlap issues with massive structs */
static AppContext g_app_ctx = {0};

AppContext *app_get_context(void) {
  if (!g_app_ctx.player) {
    g_app_ctx.player = calloc(1, sizeof(PlayerContext));
    g_app_ctx.layout = calloc(1, sizeof(WindowContext));
    g_app_ctx.running = true;
  }
  return &g_app_ctx;
}

PlayerContext *app_get_player(void) { return app_get_context()->player; }

WindowContext *app_get_layout(void) { return app_get_context()->layout; }
