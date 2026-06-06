#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include "layout.h"
#include "player.h"

/*
 * Global Application Context
 * Consolidates all major state structures into a single accessible point
 * to avoid passing dozens of individual pointers.
 */
typedef struct {
  PlayerContext *player;
  WindowContext *layout;
  bool running;
  bool headless;
  bool is_dragging_seek;
  bool is_dragging_vol;
  bool is_dragging_eq_band;
  int dragging_eq_band_idx;
} AppContext;

/* Global Accessors (Singleton-style for compatibility with existing code) */
AppContext *app_get_context(void);
PlayerContext *app_get_player(void);
WindowContext *app_get_layout(void);

#endif /* APP_CONTEXT_H */
